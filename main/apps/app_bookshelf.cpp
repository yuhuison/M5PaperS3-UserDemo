/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "apps.h"
#include "hal.h"
#include <mooncake_log.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <cJSON.h>

using namespace mooncake;

// 屏幕和布局常量
static constexpr int SCREEN_WIDTH = 540;
static constexpr int SCREEN_HEIGHT = 960;
static constexpr int PAGE_CONTENT_HEIGHT = 900;  // 页面内容高度
static constexpr int UI_HEIGHT = 60;             // 底部UI高度
static constexpr int MENU_HEIGHT = 80;

// 列表布局常量
static constexpr int LIST_HEADER_HEIGHT = 80;
static constexpr int LIST_ITEM_HEIGHT = 200;
static constexpr int LIST_PADDING = 20;
static constexpr int COVER_SIZE = 160;

// 颜色常量
static constexpr uint32_t COLOR_BG = 0xFFFFFF;
static constexpr uint32_t COLOR_TEXT = 0x000000;
static constexpr uint32_t COLOR_TEXT_GRAY = 0x666666;
static constexpr uint32_t COLOR_BORDER = 0xCCCCCC;
static constexpr uint32_t COLOR_BTN = 0xEEEEEE;
static constexpr uint32_t COLOR_PROGRESS = 0x333333;

// 翻页刷新控制
static constexpr int FULL_REFRESH_INTERVAL = 8;  // 每8页全刷新一次

void AppBookshelf::onCreate()
{
    setAppInfo().name = "AppBookshelf";
    mclog::tagInfo(getAppInfo().name, "onCreate");
    
    GetHAL().display.setRotation(0);
    _state = STATE_LOADING;
    
    loadBooks();
    
    if (_books.empty()) {
        mclog::tagInfo(getAppInfo().name, "No books found");
    } else {
        mclog::tagInfo(getAppInfo().name, "Loaded {} books", _books.size());
        _total_list_pages = (_books.size() + _books_per_page - 1) / _books_per_page;
    }
    _state = STATE_LIST;
    _need_redraw = true;
}

void AppBookshelf::onRunning()
{
    if (_need_destroy) {
        if (_app_id >= 0) {
            mooncake::GetMooncake().uninstallApp(_app_id);
        }
        return;
    }
    
    if (_state == STATE_LIST) {
        if (_need_redraw) {
            drawBookList();
            _need_redraw = false;
        }
        handleListTouch();
    } else if (_state == STATE_READING) {
        if (_need_redraw) {
            drawReading();
            _need_redraw = false;
        }
        handleReadingTouch();
    }
}

void AppBookshelf::onDestroy()
{
    mclog::tagInfo(getAppInfo().name, "onDestroy");
    
    if (_state == STATE_READING && _selected_book >= 0) {
        saveReadingProgress();
    }
    
    freeBookCovers();
    freePageImage();
}

void AppBookshelf::loadBooks()
{
    mclog::tagInfo(getAppInfo().name, "Loading books from /sdcard/books");
    
    _books.clear();
    
    DIR* dir = opendir("/sdcard/books");
    if (!dir) {
        mclog::tagError(getAppInfo().name, "Failed to open /sdcard/books");
        return;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type != DT_DIR) continue;
        if (entry->d_name[0] == '.') continue;
        
        std::string bookId = entry->d_name;
        std::string bookPath = "/sdcard/books/" + bookId;
        
        mclog::tagInfo(getAppInfo().name, "Found book: {}", bookId);
        
        // 读取 metadata.json
        std::string metadataPath = bookPath + "/metadata.json";
        FILE* f = fopen(metadataPath.c_str(), "r");
        if (!f) {
            mclog::tagError(getAppInfo().name, "Failed to open {}", metadataPath);
            continue;
        }
        
        fseek(f, 0, SEEK_END);
        size_t size = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        char* buffer = (char*)malloc(size + 1);
        fread(buffer, 1, size, f);
        buffer[size] = '\0';
        fclose(f);
        
        cJSON* json = cJSON_Parse(buffer);
        free(buffer);
        
        if (!json) {
            mclog::tagError(getAppInfo().name, "Failed to parse metadata.json");
            continue;
        }
        
        BookInfo book;
        book.id = bookId;
        
        cJSON* titleItem = cJSON_GetObjectItem(json, "title");
        cJSON* authorItem = cJSON_GetObjectItem(json, "author");
        cJSON* addedAtItem = cJSON_GetObjectItem(json, "addedAt");
        
        book.title = titleItem ? titleItem->valuestring : "未知书名";
        book.author = authorItem ? authorItem->valuestring : "未知作者";
        book.addedAt = addedAtItem ? addedAtItem->valuestring : "";
        
        // 解析 anchorMap（可选）
        cJSON* anchorMapItem = cJSON_GetObjectItem(json, "anchorMap");
        if (anchorMapItem) {
            cJSON* anchor = nullptr;
            cJSON_ArrayForEach(anchor, anchorMapItem) {
                if (anchor->string) {
                    cJSON* sectionItem = cJSON_GetObjectItem(anchor, "section");
                    cJSON* pageItem = cJSON_GetObjectItem(anchor, "page");
                    if (sectionItem && pageItem) {
                        book.anchorMap[anchor->string] = 
                            std::make_pair(sectionItem->valueint, pageItem->valueint);
                    }
                }
            }
            mclog::tagInfo(getAppInfo().name, "Loaded {} anchors for book {}", 
                          book.anchorMap.size(), bookId);
        }
        
        // 读取章节信息
        cJSON* sectionsArray = cJSON_GetObjectItem(json, "sections");
        if (sectionsArray) {
            int sectionCount = cJSON_GetArraySize(sectionsArray);
            for (int i = 0; i < sectionCount; i++) {
                cJSON* section = cJSON_GetArrayItem(sectionsArray, i);
                SectionInfo info;
                info.index = cJSON_GetObjectItem(section, "index")->valueint;
                info.title = cJSON_GetObjectItem(section, "title")->valuestring;
                info.pageCount = cJSON_GetObjectItem(section, "pageCount")->valueint;
                book.sections.push_back(info);
                
                mclog::tagInfo(getAppInfo().name, 
                              "Loaded section: index={}, title={}, pageCount={}", 
                              info.index, info.title, info.pageCount);
            }
        }
        
        cJSON_Delete(json);
        
        // 读取阅读进度
        std::string statusPath = bookPath + "/reading_status.json";
        f = fopen(statusPath.c_str(), "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            size = ftell(f);
            fseek(f, 0, SEEK_SET);
            
            buffer = (char*)malloc(size + 1);
            fread(buffer, 1, size, f);
            buffer[size] = '\0';
            fclose(f);
            
            cJSON* statusJson = cJSON_Parse(buffer);
            free(buffer);
            
            if (statusJson) {
                cJSON* secItem = cJSON_GetObjectItem(statusJson, "currentSection");
                cJSON* pageItem = cJSON_GetObjectItem(statusJson, "currentPage");
                cJSON* timeItem = cJSON_GetObjectItem(statusJson, "lastReadTime");
                
                book.currentSection = secItem ? secItem->valueint : 1;
                book.currentPage = pageItem ? pageItem->valueint : 1;
                book.lastReadTime = timeItem ? timeItem->valuestring : "";
                
                cJSON_Delete(statusJson);
            }
        } else {
            book.currentSection = 1;
            book.currentPage = 1;
            book.lastReadTime = "";
        }
        
        // 加载封面（支持 cover.png 和 COVER.png）
        std::string coverPath = bookPath + "/cover.png";
        f = fopen(coverPath.c_str(), "rb");
        if (!f) {
            coverPath = bookPath + "/COVER.png";
            f = fopen(coverPath.c_str(), "rb");
        }
        if (f) {
            fseek(f, 0, SEEK_END);
            book.coverSize = ftell(f);
            fseek(f, 0, SEEK_SET);
            
            book.coverData = (uint8_t*)malloc(book.coverSize);
            fread(book.coverData, 1, book.coverSize, f);
            fclose(f);
        }
        
        _books.push_back(std::move(book));
    }
    
    closedir(dir);
    
    // 按最后阅读时间排序（最近的在前）
    std::sort(_books.begin(), _books.end(), [](const BookInfo& a, const BookInfo& b) {
        return a.lastReadTime > b.lastReadTime;
    });
}

void AppBookshelf::drawBookList()
{
    mclog::tagInfo(getAppInfo().name, "drawBookList, page {}/{}", _list_page + 1, _total_list_pages);
    
    GetHAL().display.setEpdMode(epd_mode_t::epd_quality);
    GetHAL().display.fillScreen(COLOR_BG);
    
    // 绘制标题栏
    GetHAL().display.setFont(&fonts::efontCN_24_b);
    GetHAL().display.setTextDatum(middle_left);
    GetHAL().display.setTextColor(COLOR_TEXT);
    GetHAL().display.drawString("书架", 20, LIST_HEADER_HEIGHT / 2);
    
    // 返回按钮
    _back_btn_x = SCREEN_WIDTH - 100;
    _back_btn_y = 10;
    _back_btn_w = 80;
    _back_btn_h = 50;
    GetHAL().display.fillRoundRect(_back_btn_x, _back_btn_y, _back_btn_w, _back_btn_h, 10, COLOR_BTN);
    GetHAL().display.setFont(&fonts::efontCN_16_b);
    GetHAL().display.setTextDatum(middle_center);
    GetHAL().display.drawString("返回", _back_btn_x + _back_btn_w / 2, _back_btn_y + _back_btn_h / 2);
    
    // 分隔线
    GetHAL().display.drawLine(0, LIST_HEADER_HEIGHT, SCREEN_WIDTH, LIST_HEADER_HEIGHT, COLOR_BORDER);
    
    if (_books.empty()) {
        GetHAL().display.setFont(&fonts::efontCN_24_b);
        GetHAL().display.setTextDatum(middle_center);
        GetHAL().display.setTextColor(COLOR_TEXT_GRAY);
        GetHAL().display.drawString("暂无图书", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
        return;
    }
    
    // 绘制图书列表
    int startIdx = _list_page * _books_per_page;
    int y = LIST_HEADER_HEIGHT + LIST_PADDING;
    
    for (int i = 0; i < _books_per_page; i++) {
        int bookIdx = startIdx + i;
        if (bookIdx >= (int)_books.size()) break;
        
        drawBookItem(bookIdx, y);
        y += LIST_ITEM_HEIGHT + LIST_PADDING;
    }
    
    // 绘制分页控件
    int navY = SCREEN_HEIGHT - 80;
    GetHAL().display.setFont(&fonts::efontCN_16_b);
    
    // 上一页按钮
    _prev_list_x = 20;
    _prev_list_y = navY;
    _prev_list_w = 100;
    _prev_list_h = 50;
    if (_list_page > 0) {
        GetHAL().display.fillRoundRect(_prev_list_x, _prev_list_y, _prev_list_w, _prev_list_h, 10, COLOR_BTN);
        GetHAL().display.setTextDatum(middle_center);
        GetHAL().display.drawString("上一页", _prev_list_x + _prev_list_w / 2, _prev_list_y + _prev_list_h / 2);
    }
    
    // 页码
    char pageStr[32];
    snprintf(pageStr, sizeof(pageStr), "%d / %d", _list_page + 1, std::max(1, _total_list_pages));
    GetHAL().display.setTextDatum(middle_center);
    GetHAL().display.drawString(pageStr, SCREEN_WIDTH / 2, navY + 25);
    
    // 下一页按钮
    _next_list_x = SCREEN_WIDTH - 120;
    _next_list_y = navY;
    _next_list_w = 100;
    _next_list_h = 50;
    if (_list_page < _total_list_pages - 1) {
        GetHAL().display.fillRoundRect(_next_list_x, _next_list_y, _next_list_w, _next_list_h, 10, COLOR_BTN);
        GetHAL().display.setTextDatum(middle_center);
        GetHAL().display.drawString("下一页", _next_list_x + _next_list_w / 2, _next_list_y + _next_list_h / 2);
    }
}

void AppBookshelf::drawBookItem(int index, int y)
{
    const BookInfo& book = _books[index];
    
    // 绘制背景（确保清除旧内容）
    GetHAL().display.fillRect(LIST_PADDING, y, SCREEN_WIDTH - 2 * LIST_PADDING, LIST_ITEM_HEIGHT, COLOR_BG);
    
    // 绘制边框
    GetHAL().display.drawRect(LIST_PADDING, y, SCREEN_WIDTH - 2 * LIST_PADDING, LIST_ITEM_HEIGHT, COLOR_BORDER);
    
    // 绘制封面
    int coverX = LIST_PADDING + 10;
    int coverY = y + (LIST_ITEM_HEIGHT - COVER_SIZE) / 2;
    
    if (book.coverData && book.coverSize > 0) {
        // 封面是 540×540，缩放到 160×160
        // scale = 160.0 / 540.0 = 0.296
        GetHAL().display.drawPng(book.coverData, book.coverSize, coverX, coverY, 0, 0, 0, 0, 160.0f / 540.0f, 0.0f);
    } else {
        GetHAL().display.fillRect(coverX, coverY, COVER_SIZE, COVER_SIZE, COLOR_BTN);
        GetHAL().display.setFont(&fonts::efontCN_16_b);
        GetHAL().display.setTextDatum(middle_center);
        GetHAL().display.setTextColor(COLOR_TEXT_GRAY);
        GetHAL().display.drawString("无封面", coverX + COVER_SIZE / 2, coverY + COVER_SIZE / 2);
    }
    
    // 绘制书名和作者
    int textX = coverX + COVER_SIZE + 20;
    int textY = y + 30;
    
    GetHAL().display.setFont(&fonts::efontCN_24_b);
    GetHAL().display.setTextDatum(top_left);
    GetHAL().display.setTextColor(COLOR_TEXT);
    GetHAL().display.drawString(book.title.c_str(), textX, textY);
    
    textY += 40;
    GetHAL().display.setFont(&fonts::efontCN_16_b);
    GetHAL().display.setTextColor(COLOR_TEXT_GRAY);
    GetHAL().display.drawString(book.author.c_str(), textX, textY);
    
    // 绘制阅读进度
    textY += 35;
    int totalPages = 0;
    int currentGlobal = 0;
    for (const auto& sec : book.sections) {
        if (sec.index < book.currentSection) {
            currentGlobal += sec.pageCount;
        } else if (sec.index == book.currentSection) {
            currentGlobal += book.currentPage;
        }
        totalPages += sec.pageCount;
    }
    
    char progressStr[64];
    if (totalPages > 0) {
        int percent = currentGlobal * 100 / totalPages;
        snprintf(progressStr, sizeof(progressStr), "进度: %d%% (%d/%d页)", percent, currentGlobal, totalPages);
    } else {
        snprintf(progressStr, sizeof(progressStr), "章节: %d", (int)book.sections.size());
    }
    GetHAL().display.drawString(progressStr, textX, textY);
    
    // 绘制进度条
    textY += 30;
    int progressBarW = SCREEN_WIDTH - textX - LIST_PADDING - 30;
    int progressBarH = 10;
    GetHAL().display.drawRect(textX, textY, progressBarW, progressBarH, COLOR_BORDER);
    
    if (totalPages > 0) {
        int fillW = progressBarW * currentGlobal / totalPages;
        GetHAL().display.fillRect(textX, textY, fillW, progressBarH, COLOR_PROGRESS);
    }
}

void AppBookshelf::handleListTouch()
{
    auto touch = GetHAL().getTouchDetail();
    if (!touch.wasClicked()) return;
    
    int x = touch.x;
    int y = touch.y;
    
    // 返回按钮
    if (x >= _back_btn_x && x < _back_btn_x + _back_btn_w &&
        y >= _back_btn_y && y < _back_btn_y + _back_btn_h) {
        mclog::tagInfo(getAppInfo().name, "Back clicked");
        _need_destroy = true;
        return;
    }
    
    // 上一页
    if (_list_page > 0 &&
        x >= _prev_list_x && x < _prev_list_x + _prev_list_w &&
        y >= _prev_list_y && y < _prev_list_y + _prev_list_h) {
        _list_page--;
        _need_redraw = true;
        return;
    }
    
    // 下一页
    if (_list_page < _total_list_pages - 1 &&
        x >= _next_list_x && x < _next_list_x + _next_list_w &&
        y >= _next_list_y && y < _next_list_y + _next_list_h) {
        _list_page++;
        _need_redraw = true;
        return;
    }
    
    // 点击图书
    int startIdx = _list_page * _books_per_page;
    int itemY = LIST_HEADER_HEIGHT + LIST_PADDING;
    
    for (int i = 0; i < _books_per_page; i++) {
        int bookIdx = startIdx + i;
        if (bookIdx >= (int)_books.size()) break;
        
        if (y >= itemY && y < itemY + LIST_ITEM_HEIGHT) {
            mclog::tagInfo(getAppInfo().name, "Book {} clicked", bookIdx);
            openBook(bookIdx);
            return;
        }
        
        itemY += LIST_ITEM_HEIGHT + LIST_PADDING;
    }
}

void AppBookshelf::openBook(int bookIndex)
{
    if (bookIndex < 0 || bookIndex >= (int)_books.size()) return;
    
    _selected_book = bookIndex;
    BookInfo& book = _books[bookIndex];
    
    mclog::tagInfo(getAppInfo().name, "Opening book: {} (section {}, page {})", 
                   book.title, book.currentSection, book.currentPage);
    
    _reading_section = book.currentSection;
    _reading_page = book.currentPage;
    
    // 验证章节和页码有效性
    bool validSection = false;
    for (const auto& sec : book.sections) {
        if (sec.index == _reading_section) {
            validSection = true;
            break;
        }
    }
    
    if (!validSection || _reading_section < 0) {
        // 默认从第一个章节开始
        if (!book.sections.empty()) {
            _reading_section = book.sections[0].index;
        } else {
            _reading_section = 0;
        }
    }
    
    if (_reading_page < 1) {
        _reading_page = 1;
    }
    
    // 加载当前页面
    loadPage();
    
    _state = STATE_READING;
    _show_toc = false;
    _page_flip_count = 0;  // 重置翻页计数
    _need_redraw = true;
}

void AppBookshelf::loadPage()
{
    freePageImage();
    
    if (_selected_book < 0) return;
    const BookInfo& book = _books[_selected_book];
    
    mclog::tagInfo(getAppInfo().name, "drawReadingPage: section={}, page={}", 
                   _reading_section, _reading_page);
    
    // 构建页面文件路径: /sdcard/books/{id}/sections/{section:03d}/{page:03d}.png
    char path[256];
    snprintf(path, sizeof(path), "/sdcard/books/%s/sections/%03d/%03d.png",
             book.id.c_str(), _reading_section, _reading_page);
    
    mclog::tagInfo(getAppInfo().name, "Loading page: {}", path);
    
    FILE* f = fopen(path, "rb");
    if (!f) {
        mclog::tagError(getAppInfo().name, "Failed to open page file");
        return;
    }
    
    fseek(f, 0, SEEK_END);
    _page_image_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    _page_image = (uint8_t*)malloc(_page_image_size);
    fread(_page_image, 1, _page_image_size, f);
    fclose(f);
    
    mclog::tagInfo(getAppInfo().name, "Page loaded, size: {} bytes", _page_image_size);
    
    // 加载当前页面的链接信息
    loadPageLinks();
}

void AppBookshelf::drawReading(bool fastMode)
{
    mclog::tagInfo(getAppInfo().name, "drawReading, fastMode={}, hasImage={}", fastMode, _current_page_has_image);
    
    // 根据模式和图片标志选择刷新方式
    if (fastMode) {
        // 快速翻页模式
        if (_current_page_has_image) {
            GetHAL().display.setEpdMode(epd_mode_t::epd_text);     // 有图片用 text 模式，质量更好
        } else {
            GetHAL().display.setEpdMode(epd_mode_t::epd_fastest);  // 纯文本用最快模式
        }
    } else {
        // 全刷新模式（每8页一次）
        GetHAL().display.setEpdMode(epd_mode_t::epd_quality);  // 全刷新用高质量模式
    }
    GetHAL().display.fillScreen(COLOR_BG);
    
    // 喂狗，防止解码超时
    GetHAL().feedTheDog();
    
    if (!_page_image || _page_image_size == 0) {
        GetHAL().display.setFont(&fonts::efontCN_24_b);
        GetHAL().display.setTextDatum(middle_center);
        GetHAL().display.setTextColor(COLOR_TEXT);
        GetHAL().display.drawString("加载失败", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
        return;
    }
    
    // 绘制页面图片（540x900，显示在顶部）
    GetHAL().display.drawPng(_page_image, _page_image_size, 0, 0);
    
    // 喂狗
    GetHAL().feedTheDog();
    
    // 绘制底部UI（常驻显示）
    drawBottomBar();
    
    // 绘制链接指示器（如果有链接）
    drawLinkIndicators();
    
    // 绘制目录（如果显示）
    if (_show_toc) {
        drawTOC();
    }
}

void AppBookshelf::drawBottomBar()
{
    int barY = PAGE_CONTENT_HEIGHT;
    
    GetHAL().display.fillRect(0, barY, SCREEN_WIDTH, UI_HEIGHT, COLOR_BG);
    GetHAL().display.drawLine(0, barY, SCREEN_WIDTH, barY, COLOR_BORDER);
    
    // 按钮布局：目录 | 进度信息 | 返回
    int btnW = 80;
    int btnH = 40;
    int btnY = barY + (UI_HEIGHT - btnH) / 2;
    
    GetHAL().display.setFont(&fonts::efontCN_14);
    GetHAL().display.setTextDatum(middle_center);
    GetHAL().display.setTextColor(COLOR_TEXT);
    
    // 目录按钮（左侧）
    int btnX = 10;
    GetHAL().display.fillRoundRect(btnX, btnY, btnW, btnH, 6, COLOR_BTN);
    GetHAL().display.drawRoundRect(btnX, btnY, btnW, btnH, 6, COLOR_BORDER);
    GetHAL().display.drawString("目录", btnX + btnW / 2, btnY + btnH / 2);
    
    // 返回按钮（右侧）
    btnX = SCREEN_WIDTH - btnW - 10;
    GetHAL().display.fillRoundRect(btnX, btnY, btnW, btnH, 6, COLOR_BTN);
    GetHAL().display.drawRoundRect(btnX, btnY, btnW, btnH, 6, COLOR_BORDER);
    GetHAL().display.drawString("返回", btnX + btnW / 2, btnY + btnH / 2);
    
    // 中间显示进度信息
    int totalPages = getTotalPages();
    int currentGlobal = getCurrentGlobalPage();
    
    char info[64];
    if (_selected_book >= 0 && _reading_section > 0 && 
        _reading_section <= (int)_books[_selected_book].sections.size()) {
        snprintf(info, sizeof(info), "%d/%d页 · %d%%", 
                 currentGlobal, totalPages,
                 totalPages > 0 ? currentGlobal * 100 / totalPages : 0);
    } else {
        snprintf(info, sizeof(info), "%d/%d页", currentGlobal, totalPages);
    }
    GetHAL().display.setTextColor(COLOR_TEXT_GRAY);
    GetHAL().display.drawString(info, SCREEN_WIDTH / 2, barY + UI_HEIGHT / 2);
}

void AppBookshelf::drawTOC()
{
    if (_selected_book < 0) return;
    const BookInfo& book = _books[_selected_book];
    
    // 半透明遮罩效果 - 用较深的灰色矩形
    int tocX = 40;
    int tocY = 100;
    int tocW = SCREEN_WIDTH - 80;
    int tocH = SCREEN_HEIGHT - 200;
    
    GetHAL().display.fillRect(tocX, tocY, tocW, tocH, COLOR_BG);
    GetHAL().display.drawRect(tocX, tocY, tocW, tocH, COLOR_BORDER);
    
    // 标题
    GetHAL().display.setFont(&fonts::efontCN_24_b);
    GetHAL().display.setTextDatum(middle_center);
    GetHAL().display.setTextColor(COLOR_TEXT);
    GetHAL().display.drawString("目录", tocX + tocW / 2, tocY + 30);
    
    GetHAL().display.drawLine(tocX + 20, tocY + 55, tocX + tocW - 20, tocY + 55, COLOR_BORDER);
    
    // 章节列表（最多显示10个）
    GetHAL().display.setFont(&fonts::efontCN_16_b);
    GetHAL().display.setTextDatum(top_left);
    
    int itemY = tocY + 70;
    int itemH = 45;
    int maxItems = std::min((int)book.sections.size(), 10);
    
    for (int i = 0; i < maxItems; i++) {
        const SectionInfo& sec = book.sections[i];
        
        // 高亮当前章节
        if (sec.index == _reading_section) {
            GetHAL().display.fillRect(tocX + 10, itemY, tocW - 20, itemH - 5, COLOR_BTN);
        }
        
        // 显示章节标题
        char title[128];
        snprintf(title, sizeof(title), "%d. %s (%d页)", sec.index, sec.title.c_str(), sec.pageCount);
        GetHAL().display.setTextColor(sec.index == _reading_section ? COLOR_TEXT : COLOR_TEXT_GRAY);
        GetHAL().display.drawString(title, tocX + 20, itemY + 10);
        
        itemY += itemH;
    }
    
    // 关闭提示
    GetHAL().display.setFont(&fonts::efontCN_14);
    GetHAL().display.setTextDatum(middle_center);
    GetHAL().display.setTextColor(COLOR_TEXT_GRAY);
    GetHAL().display.drawString("点击任意位置关闭", tocX + tocW / 2, tocY + tocH - 25);
}

void AppBookshelf::handleReadingTouch()
{
    auto touch = GetHAL().getTouchDetail();
    if (!touch.wasClicked()) return;
    
    int x = touch.x;
    int y = touch.y;
    
    // 显示目录时，点击章节或关闭
    if (_show_toc) {
        const BookInfo& book = _books[_selected_book];
        
        int tocX = 40;
        int tocY = 100;
        int tocW = SCREEN_WIDTH - 80;
        int itemY = tocY + 70;
        int itemH = 45;
        int maxItems = std::min((int)book.sections.size(), 10);
        
        // 检查是否点击了章节
        for (int i = 0; i < maxItems; i++) {
            if (x >= tocX && x < tocX + tocW &&
                y >= itemY && y < itemY + itemH) {
                gotoSection(book.sections[i].index);
                _show_toc = false;
                return;
            }
            itemY += itemH;
        }
        
        // 点击其他位置关闭目录
        _show_toc = false;
        _need_redraw = true;
        return;
    }
    
    // 底部栏按钮区域
    int barY = PAGE_CONTENT_HEIGHT;
    int btnW = 80;
    int btnH = 40;
    int btnY = barY + (UI_HEIGHT - btnH) / 2;
    
    // 检查是否点击底部栏
    if (y >= barY) {
        // 目录按钮（左侧）
        if (x >= 10 && x < 10 + btnW && y >= btnY && y < btnY + btnH) {
            _show_toc = true;
            _need_redraw = true;
            return;
        }
        
        // 返回按钮（右侧）
        int returnBtnX = SCREEN_WIDTH - btnW - 10;
        if (x >= returnBtnX && x < returnBtnX + btnW && y >= btnY && y < btnY + btnH) {
            saveReadingProgress();
            _state = STATE_LIST;
            _need_redraw = true;
            return;
        }
        
        // 点击底部栏其他区域不做处理
        return;
    }
    
    // 内容区域：先检查是否点击了链接
    if (handleLinkTouch(x, y)) {
        // 链接已处理，不执行翻页
        return;
    }
    
    // 左右分区触摸翻页
    int half = SCREEN_WIDTH / 2;
    
    if (x < half) {
        prevPage();
    } else {
        nextPage();
    }
}

void AppBookshelf::nextPage()
{
    if (_selected_book < 0) return;
    const BookInfo& book = _books[_selected_book];
    
    // 查找当前章节
    const SectionInfo* currentSec = nullptr;
    for (const auto& sec : book.sections) {
        if (sec.index == _reading_section) {
            currentSec = &sec;
            break;
        }
    }
    
    if (!currentSec) {
        mclog::tagError(getAppInfo().name, "Current section {} not found", _reading_section);
        return;
    }
    
    if (_reading_page < currentSec->pageCount) {
        // 当前章节还有下一页
        _reading_page++;
    } else {
        // 跳转到下一章节
        bool foundNext = false;
        for (const auto& sec : book.sections) {
            if (sec.index > _reading_section) {
                _reading_section = sec.index;
                _reading_page = 1;
                foundNext = true;
                break;
            }
        }
        
        if (!foundNext) {
            // 已经是最后一页
            mclog::tagInfo(getAppInfo().name, "Already at last page");
            return;
        }
    }
    
    loadPage();
    _page_flip_count++;
    
    // 根据翻页次数决定刷新模式
    bool needFullRefresh = (_page_flip_count % FULL_REFRESH_INTERVAL == 0);
    drawReading(!needFullRefresh);  // fast模式 = !needFullRefresh
    
    saveReadingProgress();
}

void AppBookshelf::prevPage()
{
    if (_selected_book < 0) return;
    const BookInfo& book = _books[_selected_book];
    
    if (_reading_page > 1) {
        // 当前章节还有上一页
        _reading_page--;
    } else {
        // 跳转到上一章节的最后一页
        const SectionInfo* prevSec = nullptr;
        
        // 按index降序查找第一个小于当前section的章节
        for (int i = book.sections.size() - 1; i >= 0; i--) {
            if (book.sections[i].index < _reading_section) {
                prevSec = &book.sections[i];
                mclog::tagInfo(getAppInfo().name, 
                              "Found prev section: index={}, pageCount={}", 
                              prevSec->index, prevSec->pageCount);
                break;
            }
        }
        
        if (prevSec) {
            _reading_section = prevSec->index;
            _reading_page = prevSec->pageCount;
        } else {
            // 已经是第一页
            mclog::tagInfo(getAppInfo().name, "Already at first page");
            return;
        }
    }
    
    loadPage();
    _page_flip_count++;
    
    // 根据翻页次数决定刷新模式
    bool needFullRefresh = (_page_flip_count % FULL_REFRESH_INTERVAL == 0);
    drawReading(!needFullRefresh);  // fast模式 = !needFullRefresh
    
    saveReadingProgress();
}

void AppBookshelf::gotoSection(int sectionIndex)
{
    if (_selected_book < 0) return;
    const BookInfo& book = _books[_selected_book];
    
    // 验证章节索引是否有效（章节从 0 开始）
    bool validSection = false;
    for (const auto& sec : book.sections) {
        if (sec.index == sectionIndex) {
            validSection = true;
            break;
        }
    }
    
    if (!validSection) {
        mclog::tagError(getAppInfo().name, "Invalid section index: {}", sectionIndex);
        return;
    }
    
    mclog::tagInfo(getAppInfo().name, "Goto section {}", sectionIndex);
    
    _reading_section = sectionIndex;
    _reading_page = 1;
    _page_flip_count = 0;  // 跳转章节重置计数，使用全刷新
    
    loadPage();
    saveReadingProgress();
    _need_redraw = true;
}

void AppBookshelf::saveReadingProgress()
{
    if (_selected_book < 0 || _selected_book >= (int)_books.size()) return;
    
    BookInfo& book = _books[_selected_book];
    
    book.currentSection = _reading_section;
    book.currentPage = _reading_page;
    
    // 生成时间戳
    char timeStr[64];
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%SZ", tm_info);
    book.lastReadTime = timeStr;
    
    // 写入文件
    std::string statusPath = "/sdcard/books/" + book.id + "/reading_status.json";
    
    cJSON* json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "currentSection", _reading_section);
    cJSON_AddNumberToObject(json, "currentPage", _reading_page);
    cJSON_AddStringToObject(json, "lastReadTime", timeStr);
    
    char* jsonStr = cJSON_PrintUnformatted(json);
    
    FILE* f = fopen(statusPath.c_str(), "w");
    if (f) {
        fputs(jsonStr, f);
        fclose(f);
        mclog::tagInfo(getAppInfo().name, "Progress saved: section {}, page {}", _reading_section, _reading_page);
    }
    
    free(jsonStr);
    cJSON_Delete(json);
}

int AppBookshelf::getTotalPages()
{
    if (_selected_book < 0) return 0;
    const BookInfo& book = _books[_selected_book];
    
    int total = 0;
    for (const auto& sec : book.sections) {
        total += sec.pageCount;
    }
    return total;
}

int AppBookshelf::getCurrentGlobalPage()
{
    if (_selected_book < 0) return 0;
    const BookInfo& book = _books[_selected_book];
    
    int current = 0;
    for (const auto& sec : book.sections) {
        if (sec.index < _reading_section) {
            current += sec.pageCount;
        } else if (sec.index == _reading_section) {
            current += _reading_page;
            break;
        }
    }
    return current;
}

/* -------------------------------------------------------------------------- */
/*                              链接处理功能                                  */
/* -------------------------------------------------------------------------- */

void AppBookshelf::loadPageLinks()
{
    _current_page_links.clear();
    _current_page_has_image = false;  // 默认无图片
    
    if (_selected_book < 0) return;
    const BookInfo& book = _books[_selected_book];
    
    // 构建 links.json 路径
    char linksPath[256];
    snprintf(linksPath, sizeof(linksPath), 
             "/sdcard/books/%s/sections/%03d/links.json",
             book.id.c_str(), _reading_section);
    
    // 检查文件是否存在
    FILE* f = fopen(linksPath, "r");
    if (!f) {
        // 文件不存在是正常的，说明这个章节没有链接
        return;
    }
    
    // 读取文件内容
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* buffer = (char*)malloc(size + 1);
    fread(buffer, 1, size, f);
    buffer[size] = '\0';
    fclose(f);
    
    // 解析 JSON
    cJSON* json = cJSON_Parse(buffer);
    free(buffer);
    
    if (!json) {
        mclog::tagError(getAppInfo().name, "Failed to parse links.json");
        return;
    }
    
    // 获取 pages 数组
    cJSON* pagesArray = cJSON_GetObjectItem(json, "pages");
    if (!pagesArray) {
        cJSON_Delete(json);
        return;
    }
    
    // 查找当前页面
    int arraySize = cJSON_GetArraySize(pagesArray);
    for (int i = 0; i < arraySize; i++) {
        cJSON* pageItem = cJSON_GetArrayItem(pagesArray, i);
        cJSON* pageNum = cJSON_GetObjectItem(pageItem, "page");
        
        if (pageNum && pageNum->valueint == _reading_page) {
            // 找到当前页面，解析 hasImage 和链接
            cJSON* hasImageItem = cJSON_GetObjectItem(pageItem, "hasImage");
            if (hasImageItem && cJSON_IsBool(hasImageItem)) {
                _current_page_has_image = cJSON_IsTrue(hasImageItem);
            }
            
            cJSON* linksArray = cJSON_GetObjectItem(pageItem, "links");
            if (linksArray) {
                int linkCount = cJSON_GetArraySize(linksArray);
                for (int j = 0; j < linkCount; j++) {
                    cJSON* linkItem = cJSON_GetArrayItem(linksArray, j);
                    
                    LinkInfo link;
                    
                    // 解析基本信息
                    cJSON* text = cJSON_GetObjectItem(linkItem, "text");
                    cJSON* rect = cJSON_GetObjectItem(linkItem, "rect");
                    cJSON* href = cJSON_GetObjectItem(linkItem, "href");
                    cJSON* type = cJSON_GetObjectItem(linkItem, "type");
                    
                    if (text) link.text = text->valuestring;
                    if (href) link.href = href->valuestring;
                    if (type) link.type = type->valuestring;
                    
                    // 解析矩形区域
                    if (rect) {
                        cJSON* x = cJSON_GetObjectItem(rect, "x");
                        cJSON* y = cJSON_GetObjectItem(rect, "y");
                        cJSON* w = cJSON_GetObjectItem(rect, "width");
                        cJSON* h = cJSON_GetObjectItem(rect, "height");
                        
                        link.x = x ? x->valueint : 0;
                        link.y = y ? y->valueint : 0;
                        link.w = w ? w->valueint : 0;
                        link.h = h ? h->valueint : 0;
                    }
                    
                    // 解析目标位置（内部链接）
                    if (link.type == "internal") {
                        cJSON* target = cJSON_GetObjectItem(linkItem, "target");
                        if (target) {
                            cJSON* sec = cJSON_GetObjectItem(target, "section");
                            cJSON* page = cJSON_GetObjectItem(target, "page");
                            link.targetSection = sec ? sec->valueint : 0;
                            link.targetPage = page ? page->valueint : 0;
                        }
                    }
                    
                    _current_page_links.push_back(link);
                }
                
                mclog::tagInfo(getAppInfo().name, 
                              "Loaded {} links for section {}, page {}", 
                              _current_page_links.size(), _reading_section, _reading_page);
            }
            break;
        }
    }
    
    cJSON_Delete(json);
}

void AppBookshelf::drawLinkIndicators()
{
    if (_current_page_links.empty()) return;
    
    auto& lcd = GetHAL().display;
    
    // 使用浅灰色绘制链接下划线
    lcd.setColor(0xAAAAAA);
    
    for (const auto& link : _current_page_links) {
        // 在链接文本下方绘制1像素的下划线
        int underlineY = link.y + link.h - 2;
        lcd.drawLine(link.x, underlineY, link.x + link.w, underlineY);
        
        // 可选：在链接周围绘制虚线边框（用于调试）
        // lcd.drawRect(link.x, link.y, link.w, link.h);
    }
}

bool AppBookshelf::handleLinkTouch(int x, int y)
{
    if (_current_page_links.empty()) return false;
    
    // 检查触摸点是否在任何链接区域内
    for (const auto& link : _current_page_links) {
        // Y轴扩展5像素容错
        int expandY = 5;
        if (x >= link.x && x <= (link.x + link.w) &&
            y >= (link.y - expandY) && y <= (link.y + link.h + expandY)) {
            
            mclog::tagInfo(getAppInfo().name, 
                          "Link clicked: type={}, href={}", 
                          link.type, link.href);
            
            if (link.type == "internal") {
                // 内部跳转
                if (link.targetSection > 0 && link.targetPage > 0) {
                    _reading_section = link.targetSection;
                    _reading_page = link.targetPage;
                    
                    freePageImage();
                    loadPage();
                    _need_redraw = true;
                    
                    mclog::tagInfo(getAppInfo().name, 
                                  "Jump to section {}, page {}", 
                                  _reading_section, _reading_page);
                    return true;
                }
            } else if (link.type == "external") {
                // 外部链接，显示提示
                auto& lcd = GetHAL().display;
                lcd.setEpdMode(epd_fastest);
                
                // 在屏幕中央显示提示框
                int boxW = 400;
                int boxH = 150;
                int boxX = (SCREEN_WIDTH - boxW) / 2;
                int boxY = (PAGE_CONTENT_HEIGHT - boxH) / 2;
                
                lcd.fillRect(boxX, boxY, boxW, boxH, COLOR_BG);
                lcd.drawRect(boxX, boxY, boxW, boxH, COLOR_BORDER);
                
                lcd.setFont(&fonts::efontCN_16_b);
                lcd.setTextColor(COLOR_TEXT);
                lcd.setTextDatum(middle_center);
                lcd.drawString("外部链接", SCREEN_WIDTH / 2, boxY + 40);
                lcd.drawString("设备不支持访问", SCREEN_WIDTH / 2, boxY + 70);
                lcd.drawString("点击任意处继续", SCREEN_WIDTH / 2, boxY + 100);
                
                lcd.display();
                
                // 等待触摸后恢复
                vTaskDelay(pdMS_TO_TICKS(2000));
                _need_redraw = true;
                return true;
            }
        }
    }
    
    return false;
}

void AppBookshelf::jumpToAnchor(const std::string& anchor)
{
    if (_selected_book < 0) return;
    const BookInfo& book = _books[_selected_book];
    
    // 查找锚点
    auto it = book.anchorMap.find(anchor);
    if (it != book.anchorMap.end()) {
        int targetSection = it->second.first;
        int targetPage = it->second.second;
        
        mclog::tagInfo(getAppInfo().name, 
                      "Jump to anchor '{}' -> section {}, page {}", 
                      anchor, targetSection, targetPage);
        
        _reading_section = targetSection;
        _reading_page = targetPage;
        
        freePageImage();
        loadPage();
        _need_redraw = true;
    } else {
        mclog::tagWarn(getAppInfo().name, 
                      "Anchor '{}' not found in anchorMap", anchor);
    }
}

void AppBookshelf::freeBookCovers()
{
    for (auto& book : _books) {
        if (book.coverData) {
            free(book.coverData);
            book.coverData = nullptr;
            book.coverSize = 0;
        }
    }
}

void AppBookshelf::freePageImage()
{
    if (_page_image) {
        free(_page_image);
        _page_image = nullptr;
        _page_image_size = 0;
    }
}
