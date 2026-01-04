/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "apps.h"
#include <mooncake_log.h>
#include <assets.h>
#include <hal.h>
#include <lgfx/v1/lgfx_fonts.hpp>  // For efontCN

using namespace mooncake;

// 屏幕尺寸 (竖屏)
static const int SCREEN_WIDTH = 540;
static const int SCREEN_HEIGHT = 960;

// 状态栏
static const int STATUS_BAR_HEIGHT = 60;
static const int STATUS_BAR_PADDING = 20;

// 书架卡片
static const int CARD_MARGIN = 30;
static const int CARD_PADDING = 20;
static const int COVER_SIZE = 180;

// 颜色定义
static const uint32_t COLOR_BG = 0xFFFFFF;         // 白色背景
static const uint32_t COLOR_BORDER = 0xCCCCCC;     // 边框颜色
static const uint32_t COLOR_SHADOW = 0x333333;     // 阴影颜色
static const uint32_t COLOR_TEXT = 0x000000;       // 黑色文字
static const uint32_t COLOR_TEXT_GRAY = 0x666666;  // 灰色文字
static const uint32_t COLOR_PROGRESS_BG = 0xCCCCCC;
static const uint32_t COLOR_PROGRESS = 0x333333;
static const uint32_t COLOR_BTN_BG = 0xE0E0E0;     // 按钮背景

static const uint32_t COLOR_TEXT_WHITE = 0xFFFFFF; // 白色文字
static const uint32_t COLOR_BG_DARK = 0x444444; // 深色背景

void AppHome::onCreate()
{
    setAppInfo().name = "AppHome";
    mclog::tagInfo(getAppInfo().name, "onCreate");

    // 设置竖屏
    GetHAL().display.setRotation(0);
    
    // 初始化SD卡（用于WiFi配置保存等）
    mclog::tagInfo(getAppInfo().name, "Checking SD card status...");
    if (!GetHAL().isSdCardMounted()) {
        mclog::tagInfo(getAppInfo().name, "SD card not mounted, initializing...");
        GetHAL().sdCardTest();
        if (GetHAL().isSdCardMounted()) {
            mclog::tagInfo(getAppInfo().name, "SD card initialized successfully");
        } else {
            mclog::tagError(getAppInfo().name, "SD card initialization failed!");
        }
    } else {
        mclog::tagInfo(getAppInfo().name, "SD card already mounted");
    }
    
    _need_full_refresh = true;
    open();
}

void AppHome::onRunning()
{
    // 定时更新状态栏时间
    if (GetHAL().millis() - _time_update_count > 60000 || _need_full_refresh) {
        updateTime();
        _time_update_count = GetHAL().millis();
    }
    
    // 定时更新电池
    if (GetHAL().millis() - _battery_update_count > 5000 || _need_full_refresh) {
        updateBattery();
        _battery_update_count = GetHAL().millis();
    }
    
    // 全屏刷新
    if (_need_full_refresh) {
        drawFullUI();
        _need_full_refresh = false;
    }
    
    // 检测触摸
    handleTouch();
}

void AppHome::drawFullUI()
{
    mclog::tagInfo(getAppInfo().name, "Drawing full UI");
    
    GetHAL().display.setEpdMode(epd_mode_t::epd_quality);
    
    // 绘制背景
    GetHAL().display.fillScreen(COLOR_BG);
    
    // 绘制状态栏
    drawStatusBar();
    
    // 绘制三个板块
    drawBookshelfCard();
    drawPushCard();
    drawLifeCard();
    
    // 绘制底部按钮
    drawBottomButtons();
}

void AppHome::drawStatusBar()
{
    // 状态栏背景（与整体背景同色）
    GetHAL().display.fillRect(0, 0, SCREEN_WIDTH, STATUS_BAR_HEIGHT, COLOR_BG);
    
    // 左侧：日期时间
    updateTime();
    
    // 右侧：电池
    updateBattery();
}

void AppHome::updateTime()
{
    auto rtc_time = GetHAL().rtc.getDateTime();
    
    // 格式化时间字符串: "2026年1月1日 17:56"
    char time_str[64];
    snprintf(time_str, sizeof(time_str), "%d年%d月%d日 %02d:%02d",
             rtc_time.date.year, rtc_time.date.month, rtc_time.date.date,
             rtc_time.time.hours, rtc_time.time.minutes);
    
    GetHAL().display.setEpdMode(epd_mode_t::epd_fast);
    GetHAL().display.setFont(&fonts::efontCN_14);
    GetHAL().display.setTextDatum(middle_left);
    GetHAL().display.setTextColor(COLOR_TEXT, COLOR_BG);
    
    // 清除旧文字区域
    GetHAL().display.fillRect(STATUS_BAR_PADDING, 0, 300, STATUS_BAR_HEIGHT, COLOR_BG);
    GetHAL().display.drawString(time_str, STATUS_BAR_PADDING, STATUS_BAR_HEIGHT / 2);
}

void AppHome::updateBattery()
{
    float voltage = GetHAL().getBatteryVoltage();
    // 简单的电量估算 (3.3V=0%, 4.2V=100%)
    int percent = (int)((voltage - 3.3f) / 0.9f * 100);
    if (percent > 100) percent = 100;
    if (percent < 0) percent = 0;
    
    char bat_str[16];
    snprintf(bat_str, sizeof(bat_str), "%d%%", percent);
    
    GetHAL().display.setEpdMode(epd_mode_t::epd_fast);
    GetHAL().display.setFont(&fonts::efontCN_14);
    GetHAL().display.setTextDatum(middle_right);
    GetHAL().display.setTextColor(COLOR_TEXT, COLOR_BG);
    
    // 绘制电池图标（简单矩形）
    int bat_x = SCREEN_WIDTH - STATUS_BAR_PADDING - 70;
    int bat_y = STATUS_BAR_HEIGHT / 2 - 12;
    int bat_w = 40;
    int bat_h = 24;
    
    // 清除旧区域
    GetHAL().display.fillRect(bat_x - 10, bat_y - 5, bat_w + 80, bat_h + 10, COLOR_BG);
    
    // 电池外框
    GetHAL().display.drawRect(bat_x, bat_y, bat_w, bat_h, COLOR_TEXT);
    GetHAL().display.fillRect(bat_x + bat_w, bat_y + 6, 4, 12, COLOR_TEXT);
    
    // 电池填充
    int fill_w = (bat_w - 4) * percent / 100;
    GetHAL().display.fillRect(bat_x + 2, bat_y + 2, fill_w, bat_h - 4, COLOR_TEXT);
    
    // 电量百分比文字
    GetHAL().display.drawString(bat_str, SCREEN_WIDTH - STATUS_BAR_PADDING, STATUS_BAR_HEIGHT / 2);
}

void AppHome::drawBookshelfCard()
{
    auto& lcd = GetHAL().display;
    
    // 整体卡片布局
    int card_x = CARD_MARGIN;
    int card_bottom = STATUS_BAR_HEIGHT + 20 + 180;  // 底部对齐基准（上移）
    
    // 左侧封面框尺寸 (更高)
    int cover_w = COVER_SIZE;
    int cover_h = 180;  // 比详情框更高
    int cover_x = card_x;
    int cover_y = card_bottom - cover_h;  // 底部对齐
    
    // 右侧详情框尺寸 (紧贴左侧框，无间隙)
    int detail_h = 160;  // 比封面框矮
    int detail_x = cover_x + cover_w;  // 紧贴左侧框
    int detail_y = card_bottom - detail_h;  // 底部对齐
    int detail_w = SCREEN_WIDTH - CARD_MARGIN - detail_x;
    
    // ========== 左侧封面框 ==========
    // 白色填充
    lcd.fillRect(cover_x, cover_y, cover_w, cover_h, COLOR_BG);
    
    // #CCCCCC 描边 (左、上、下三边，右边与详情框共享)
    lcd.drawFastVLine(cover_x, cover_y, cover_h, COLOR_BORDER);  // 左边
    lcd.drawFastHLine(cover_x, cover_y, cover_w, COLOR_BORDER);  // 上边
    lcd.drawFastHLine(cover_x, cover_y + cover_h - 1, cover_w, COLOR_BORDER);  // 下边
    
    // 内嵌阴影: 上方最粗(8px)，左边和下边其次(6px)，右边最细(2px)
    // 上方阴影 (最粗 8px)
    for (int i = 1; i <= 8; i++) {
        uint32_t shadow_color = (i <= 4) ? COLOR_SHADOW : 0x666666;
        lcd.drawFastHLine(cover_x + 1, cover_y + i, cover_w - 2, shadow_color);
    }
    // 左边阴影 (6px)
    for (int i = 1; i <= 6; i++) {
        uint32_t shadow_color = (i <= 3) ? COLOR_SHADOW : 0x666666;
        lcd.drawFastVLine(cover_x + i, cover_y + 1, cover_h - 2, shadow_color);
    }
    // 下边阴影 (6px)
    for (int i = 1; i <= 6; i++) {
        uint32_t shadow_color = (i <= 2) ? 0x666666 : 0x999999;
        lcd.drawFastHLine(cover_x + 1, cover_y + cover_h - 1 - i, cover_w - 2, shadow_color);
    }
    // 右边阴影 (2px)
    for (int i = 1; i <= 2; i++) {
        lcd.drawFastVLine(cover_x + cover_w - 1 - i, cover_y + 1, cover_h - 2, 0x999999);
    }
    
    // ========== 右侧详情框 ==========
    // 白色填充
    lcd.fillRect(detail_x, detail_y, detail_w, detail_h, COLOR_BG);
    
    // #CCCCCC 描边 (上、右、下三边，左边与封面框共享)
    lcd.drawFastHLine(detail_x, detail_y, detail_w, COLOR_BORDER);  // 上边
    lcd.drawFastVLine(detail_x + detail_w - 1, detail_y, detail_h, COLOR_BORDER);  // 右边
    lcd.drawFastHLine(detail_x, detail_y + detail_h - 1, detail_w, COLOR_BORDER);  // 下边
    // 连接处的边框线
    lcd.drawFastVLine(detail_x, detail_y, detail_h, COLOR_BORDER);  // 左边（与封面框连接）
    
    // 内嵌阴影: 右侧最粗(8px)，上方其次(6px)，下方最细(2px)
    // 右侧阴影 (最粗 8px)
    for (int i = 1; i <= 8; i++) {
        uint32_t shadow_color = (i <= 4) ? COLOR_SHADOW : 0x666666;
        lcd.drawFastVLine(detail_x + detail_w - 1 - i, detail_y + 1, detail_h - 2, shadow_color);
    }
    // 上方阴影 (6px)
    for (int i = 1; i <= 6; i++) {
        uint32_t shadow_color = (i <= 3) ? COLOR_SHADOW : 0x666666;
        lcd.drawFastHLine(detail_x + 1, detail_y + i, detail_w - 2, shadow_color);
    }
    // 下方阴影 (2px)
    for (int i = 1; i <= 2; i++) {
        lcd.drawFastHLine(detail_x + 1, detail_y + detail_h - 1 - i, detail_w - 2, 0x999999);
    }
    
    // ========== 右侧内容 ==========
    int text_x = detail_x + 15;
    int text_y = detail_y + 20;
    
    // "上次阅读:"
    lcd.setFont(&fonts::efontCN_16_b);
    lcd.setTextDatum(top_left);
    lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    lcd.drawString("上次阅读:", text_x, text_y);
    
    // 书名 "葬送的芙莉莲:"
    lcd.setFont(&fonts::efontCN_24_b);
    lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    lcd.drawString("葬送的芙莉莲:", text_x, text_y + 25);
    
    // 进度条
    int progress_y = text_y + 60;
    int progress_w = detail_w - 40;
    int progress_h = 14;
    int progress_percent = 59;  // 212/360 ≈ 59%
    
    lcd.fillRect(text_x, progress_y, progress_w, progress_h, COLOR_PROGRESS);
    lcd.fillRect(text_x + progress_w * progress_percent / 100, progress_y, 
                 progress_w - progress_w * progress_percent / 100, progress_h, COLOR_PROGRESS_BG);
    
    // "黄金乡篇, 212 / 360"
    lcd.setFont(&fonts::efontCN_16_b);
    lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    lcd.drawString("黄金乡篇, 212 / 360", text_x, progress_y + 20);
    
    // 正在读数量 (带圆点)
    int reading_y = progress_y + 50;
    lcd.fillCircle(text_x + 6, reading_y + 7, 6, COLOR_TEXT);
    lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    lcd.drawString("正在读: 25本", text_x + 20, reading_y);
    
    // "书架" 标签 (详情框右上角，黑底白字)
    int label_w = 60;
    int label_h = 28;
    int label_x = detail_x + detail_w - label_w - 4;
    int label_y = detail_y + 4;
    
    lcd.fillRect(label_x, label_y, label_w, label_h, COLOR_BG_DARK);
    lcd.setFont(&fonts::efontCN_16_b);
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(COLOR_TEXT_WHITE, COLOR_BG_DARK);
    lcd.drawString("书架", label_x + label_w / 2, label_y + label_h / 2);
    
    // 保存整个详情框区域用于触摸检测
    _bookshelf_btn_x = detail_x;
    _bookshelf_btn_y = detail_y;
    _bookshelf_btn_w = detail_w;
    _bookshelf_btn_h = detail_h;
}

void AppHome::handleTouch()
{
    // 检测书架板块点击
    if (GetHAL().wasTouchClickedArea(_bookshelf_btn_x, _bookshelf_btn_y, 
                                      _bookshelf_btn_w, _bookshelf_btn_h)) {
        mclog::tagInfo(getAppInfo().name, "Bookshelf card clicked");
        GetHAL().tone(3000, 50);
        
        // 创建并安装书架APP
        auto bookshelfApp = std::make_unique<AppBookshelf>();
        auto* appPtr = bookshelfApp.get();
        int appId = GetMooncake().installApp(std::move(bookshelfApp));
        appPtr->setAppId(appId);
        mclog::tagInfo(getAppInfo().name, "Created Bookshelf app with ID: %d", appId);
        GetMooncake().openApp(appId);
        close();
        return;
    }
    
    // 检测推送板块点击
    if (GetHAL().wasTouchClickedArea(_push_btn_x, _push_btn_y, 
                                      _push_btn_w, _push_btn_h)) {
        mclog::tagInfo(getAppInfo().name, "Push card clicked");
        GetHAL().tone(3000, 50);
        // TODO: 打开推送页面
    }
    
    // 检测生活板块点击
    if (GetHAL().wasTouchClickedArea(_life_btn_x, _life_btn_y, 
                                      _life_btn_w, _life_btn_h)) {
        mclog::tagInfo(getAppInfo().name, "Life card clicked");
        GetHAL().tone(3000, 50);
        // TODO: 打开生活/AI助手页面
    }
    
    // 检测WiFi配置按钮点击
    if (GetHAL().wasTouchClickedArea(_wifi_btn_x, _wifi_btn_y, 
                                      _wifi_btn_w, _wifi_btn_h)) {
        mclog::tagInfo(getAppInfo().name, "WiFi config button clicked");
        GetHAL().tone(3000, 50);
        // 启动WiFi配置App，先创建实例以便设置App ID
        auto wifi_app = std::make_unique<AppWifiConfig>();
        AppWifiConfig* wifi_app_ptr = wifi_app.get();
        int app_id = mooncake::GetMooncake().installApp(std::move(wifi_app));
        wifi_app_ptr->setAppId(app_id);  // 设置App ID以便它能卸载自己
        mooncake::GetMooncake().openApp(app_id);
        close();  // 关闭Home App，防止继续处理触摸事件
        return;
    }
    
    // 检测USB File传输按钮点击
    if (GetHAL().wasTouchClickedArea(_usb_btn_x, _usb_btn_y, 
                                      _usb_btn_w, _usb_btn_h)) {
        mclog::tagInfo(getAppInfo().name, "USB File button clicked");
        GetHAL().tone(3000, 50);
        // 启动USB文件传输App
        auto usb_app = std::make_unique<AppUsbFile>();
        AppUsbFile* usb_app_ptr = usb_app.get();
        int app_id = mooncake::GetMooncake().installApp(std::move(usb_app));
        usb_app_ptr->setAppId(app_id);
        mooncake::GetMooncake().openApp(app_id);
        close();
        return;
    }
}

void AppHome::drawBottomButtons()
{
    auto& lcd = GetHAL().display;
    
    // 底部按钮区域起始位置
    int btn_area_y = STATUS_BAR_HEIGHT + 20 + 180 + 40 + 160 + 40 + 160 + 40;  // 生活板块底部 + 间距
    int btn_h = 60;
    int btn_margin = 30;
    int btn_gap = 20;  // 按钮之间的间距
    
    // WiFi配置按钮
    int wifi_btn_w = 150;
    int wifi_btn_x = btn_margin;
    int wifi_btn_y = btn_area_y;
    
    // 绘制按钮背景和边框
    lcd.fillRect(wifi_btn_x, wifi_btn_y, wifi_btn_w, btn_h, COLOR_BG);
    lcd.drawRect(wifi_btn_x, wifi_btn_y, wifi_btn_w, btn_h, COLOR_BORDER);
    
    // 内嵌阴影效果
    for (int i = 1; i <= 4; i++) {
        uint32_t shadow_color = (i <= 2) ? COLOR_SHADOW : 0x666666;
        lcd.drawFastHLine(wifi_btn_x + 1, wifi_btn_y + i, wifi_btn_w - 2, shadow_color);
        lcd.drawFastVLine(wifi_btn_x + i, wifi_btn_y + 1, btn_h - 2, shadow_color);
    }
    
    // WiFi图标（简单绘制）
    int icon_x = wifi_btn_x + 20;
    int icon_y = wifi_btn_y + btn_h / 2;
    lcd.fillCircle(icon_x, icon_y + 10, 4, COLOR_TEXT);
    lcd.drawArc(icon_x, icon_y + 10, 10, 8, 225, 315, COLOR_TEXT);
    lcd.drawArc(icon_x, icon_y + 10, 18, 16, 225, 315, COLOR_TEXT);
    
    // 按钮文字
    lcd.setFont(&fonts::efontCN_16_b);
    lcd.setTextDatum(middle_left);
    lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    lcd.drawString("WiFi", wifi_btn_x + 45, wifi_btn_y + btn_h / 2);
    
    // 保存触摸区域
    _wifi_btn_x = wifi_btn_x;
    _wifi_btn_y = wifi_btn_y;
    _wifi_btn_w = wifi_btn_w;
    _wifi_btn_h = btn_h;
    
    // USB File 传输按钮
    int usb_btn_w = 150;
    int usb_btn_x = wifi_btn_x + wifi_btn_w + btn_gap;
    int usb_btn_y = btn_area_y;
    
    // 绘制按钮背景和边框
    lcd.fillRect(usb_btn_x, usb_btn_y, usb_btn_w, btn_h, COLOR_BG);
    lcd.drawRect(usb_btn_x, usb_btn_y, usb_btn_w, btn_h, COLOR_BORDER);
    
    // 内嵌阴影效果
    for (int i = 1; i <= 4; i++) {
        uint32_t shadow_color = (i <= 2) ? COLOR_SHADOW : 0x666666;
        lcd.drawFastHLine(usb_btn_x + 1, usb_btn_y + i, usb_btn_w - 2, shadow_color);
        lcd.drawFastVLine(usb_btn_x + i, usb_btn_y + 1, btn_h - 2, shadow_color);
    }
    
    // USB图标（简单的USB符号）
    int usb_icon_x = usb_btn_x + 20;
    int usb_icon_y = usb_btn_y + btn_h / 2;
    // 绘制USB符号 - 简化版
    lcd.fillRect(usb_icon_x - 3, usb_icon_y - 8, 6, 16, COLOR_TEXT);  // 主干
    lcd.fillRect(usb_icon_x - 8, usb_icon_y - 12, 16, 4, COLOR_TEXT);  // 顶部横条
    lcd.fillCircle(usb_icon_x - 6, usb_icon_y - 14, 2, COLOR_TEXT);  // 左上圆点
    lcd.fillCircle(usb_icon_x + 6, usb_icon_y - 14, 2, COLOR_TEXT);  // 右上圆点
    lcd.fillTriangle(usb_icon_x, usb_icon_y + 8, usb_icon_x - 5, usb_icon_y + 14, usb_icon_x + 5, usb_icon_y + 14, COLOR_TEXT);  // 底部箭头
    
    // 按钮文字
    lcd.setFont(&fonts::efontCN_16_b);
    lcd.setTextDatum(middle_left);
    lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    lcd.drawString("USB", usb_btn_x + 45, usb_btn_y + btn_h / 2);
    
    // 保存触摸区域
    _usb_btn_x = usb_btn_x;
    _usb_btn_y = usb_btn_y;
    _usb_btn_w = usb_btn_w;
    _usb_btn_h = btn_h;
}

void AppHome::drawPushCard()
{
    auto& lcd = GetHAL().display;
    
    // 整体卡片布局 (在书架板块下方，增加间隙)
    int card_bottom = STATUS_BAR_HEIGHT + 20 + 180 + 40 + 160;  // 书架板块底部 + 更大间距 + 高度
    
    // 推送板块：封面框在右侧（交错布局）
    int cover_w = COVER_SIZE;
    int cover_h = 180;
    int cover_x = SCREEN_WIDTH - CARD_MARGIN - cover_w;  // 右侧
    int cover_y = card_bottom - cover_h;
    
    // 左侧详情框尺寸
    int detail_h = 160;
    int detail_x = CARD_MARGIN;  // 左侧
    int detail_y = card_bottom - detail_h;
    int detail_w = cover_x - CARD_MARGIN;  // 到封面框左边
    
    // ========== 左侧详情框 ==========
    lcd.fillRect(detail_x, detail_y, detail_w, detail_h, COLOR_BG);
    
    // 描边
    lcd.drawFastVLine(detail_x, detail_y, detail_h, COLOR_BORDER);  // 左边
    lcd.drawFastHLine(detail_x, detail_y, detail_w, COLOR_BORDER);  // 上边
    lcd.drawFastHLine(detail_x, detail_y + detail_h - 1, detail_w, COLOR_BORDER);  // 下边
    lcd.drawFastVLine(detail_x + detail_w - 1, detail_y, detail_h, COLOR_BORDER);  // 右边（与封面框连接）
    
    // 内嵌阴影: 左侧最粗(8px)，上方其次(6px)，下方最细(2px)
    for (int i = 1; i <= 8; i++) {
        uint32_t shadow_color = (i <= 4) ? COLOR_SHADOW : 0x666666;
        lcd.drawFastVLine(detail_x + i, detail_y + 1, detail_h - 2, shadow_color);
    }
    for (int i = 1; i <= 6; i++) {
        uint32_t shadow_color = (i <= 3) ? COLOR_SHADOW : 0x666666;
        lcd.drawFastHLine(detail_x + 1, detail_y + i, detail_w - 2, shadow_color);
    }
    for (int i = 1; i <= 2; i++) {
        lcd.drawFastHLine(detail_x + 1, detail_y + detail_h - 1 - i, detail_w - 2, 0x999999);
    }
    
    // ========== 右侧封面框 ==========
    lcd.fillRect(cover_x, cover_y, cover_w, cover_h, COLOR_BG);
    
    // 描边 (右、上、下三边)
    lcd.drawFastVLine(cover_x + cover_w - 1, cover_y, cover_h, COLOR_BORDER);  // 右边
    lcd.drawFastHLine(cover_x, cover_y, cover_w, COLOR_BORDER);  // 上边
    lcd.drawFastHLine(cover_x, cover_y + cover_h - 1, cover_w, COLOR_BORDER);  // 下边
    
    // 内嵌阴影: 上方最粗(8px)，右边和下边其次(6px)，左边最细(2px)
    for (int i = 1; i <= 8; i++) {
        uint32_t shadow_color = (i <= 4) ? COLOR_SHADOW : 0x666666;
        lcd.drawFastHLine(cover_x + 1, cover_y + i, cover_w - 2, shadow_color);
    }
    for (int i = 1; i <= 6; i++) {
        uint32_t shadow_color = (i <= 3) ? COLOR_SHADOW : 0x666666;
        lcd.drawFastVLine(cover_x + cover_w - 1 - i, cover_y + 1, cover_h - 2, shadow_color);
    }
    for (int i = 1; i <= 6; i++) {
        uint32_t shadow_color = (i <= 2) ? 0x666666 : 0x999999;
        lcd.drawFastHLine(cover_x + 1, cover_y + cover_h - 1 - i, cover_w - 2, shadow_color);
    }
    for (int i = 1; i <= 2; i++) {
        lcd.drawFastVLine(cover_x + i, cover_y + 1, cover_h - 2, 0x999999);
    }
    
    // ========== 左侧内容 ==========
    int text_x = detail_x + 25;  // 增加左边距
    int text_y = detail_y + 20;
    
    lcd.setFont(&fonts::efontCN_16_b);
    lcd.setTextDatum(top_left);
    lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    lcd.drawString("最新推送:", text_x, text_y);
    
    lcd.setFont(&fonts::efontCN_24_b);
    lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    lcd.drawString("今日科技要闻", text_x, text_y + 25);
    
    lcd.setFont(&fonts::efontCN_16_b);
    lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    lcd.drawString("来自: 36氪", text_x, text_y + 60);
    
    int reading_y = text_y + 90;
    lcd.fillCircle(text_x + 6, reading_y + 7, 6, COLOR_TEXT);
    lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    lcd.drawString("未读: 12篇", text_x + 20, reading_y);
    
    // "推送" 标签 (详情框右上角)
    int label_w = 60;
    int label_h = 28;
    int label_x = detail_x + detail_w - label_w - 1;
    int label_y = detail_y + 4;
    
    lcd.fillRect(label_x, label_y, label_w, label_h, COLOR_BG_DARK);
    lcd.setFont(&fonts::efontCN_16_b);
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(COLOR_TEXT_WHITE, COLOR_BG_DARK);
    lcd.drawString("推送", label_x + label_w / 2, label_y + label_h / 2);
    
    // 保存触摸区域
    _push_btn_x = detail_x;
    _push_btn_y = detail_y;
    _push_btn_w = detail_w;
    _push_btn_h = detail_h;
}

void AppHome::drawLifeCard()
{
    auto& lcd = GetHAL().display;
    
    // 整体卡片布局 (在推送板块下方，增加间隙)
    int card_x = CARD_MARGIN;
    int card_bottom = STATUS_BAR_HEIGHT + 20 + 180 + 40 + 160 + 40 + 160;  // 推送板块底部 + 更大间距 + 高度
    
    // 左侧封面框尺寸 (更高)
    int cover_w = COVER_SIZE;
    int cover_h = 180;
    int cover_x = card_x;
    int cover_y = card_bottom - cover_h;
    
    // 右侧详情框尺寸
    int detail_h = 160;
    int detail_x = cover_x + cover_w;
    int detail_y = card_bottom - detail_h;
    int detail_w = SCREEN_WIDTH - CARD_MARGIN - detail_x;
    
    // ========== 左侧封面框 ==========
    lcd.fillRect(cover_x, cover_y, cover_w, cover_h, COLOR_BG);
    
    // 描边
    lcd.drawFastVLine(cover_x, cover_y, cover_h, COLOR_BORDER);
    lcd.drawFastHLine(cover_x, cover_y, cover_w, COLOR_BORDER);
    lcd.drawFastHLine(cover_x, cover_y + cover_h - 1, cover_w, COLOR_BORDER);
    
    // 内嵌阴影
    for (int i = 1; i <= 8; i++) {
        uint32_t shadow_color = (i <= 4) ? COLOR_SHADOW : 0x666666;
        lcd.drawFastHLine(cover_x + 1, cover_y + i, cover_w - 2, shadow_color);
    }
    for (int i = 1; i <= 6; i++) {
        uint32_t shadow_color = (i <= 3) ? COLOR_SHADOW : 0x666666;
        lcd.drawFastVLine(cover_x + i, cover_y + 1, cover_h - 2, shadow_color);
    }
    for (int i = 1; i <= 6; i++) {
        uint32_t shadow_color = (i <= 2) ? 0x666666 : 0x999999;
        lcd.drawFastHLine(cover_x + 1, cover_y + cover_h - 1 - i, cover_w - 2, shadow_color);
    }
    for (int i = 1; i <= 2; i++) {
        lcd.drawFastVLine(cover_x + cover_w - 1 - i, cover_y + 1, cover_h - 2, 0x999999);
    }
    
    // ========== 右侧详情框 ==========
    lcd.fillRect(detail_x, detail_y, detail_w, detail_h, COLOR_BG);
    
    // 描边
    lcd.drawFastHLine(detail_x, detail_y, detail_w, COLOR_BORDER);
    lcd.drawFastVLine(detail_x + detail_w - 1, detail_y, detail_h, COLOR_BORDER);
    lcd.drawFastHLine(detail_x, detail_y + detail_h - 1, detail_w, COLOR_BORDER);
    lcd.drawFastVLine(detail_x, detail_y, detail_h, COLOR_BORDER);
    
    // 内嵌阴影
    for (int i = 1; i <= 8; i++) {
        uint32_t shadow_color = (i <= 4) ? COLOR_SHADOW : 0x666666;
        lcd.drawFastVLine(detail_x + detail_w - 1 - i, detail_y + 1, detail_h - 2, shadow_color);
    }
    for (int i = 1; i <= 6; i++) {
        uint32_t shadow_color = (i <= 3) ? COLOR_SHADOW : 0x666666;
        lcd.drawFastHLine(detail_x + 1, detail_y + i, detail_w - 2, shadow_color);
    }
    for (int i = 1; i <= 2; i++) {
        lcd.drawFastHLine(detail_x + 1, detail_y + detail_h - 1 - i, detail_w - 2, 0x999999);
    }
    
    // ========== 右侧内容 ==========
    int text_x = detail_x + 15;
    int text_y = detail_y + 20;
    
    lcd.setFont(&fonts::efontCN_16_b);
    lcd.setTextDatum(top_left);
    lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    lcd.drawString("AI 助手:", text_x, text_y);
    
    lcd.setFont(&fonts::efontCN_24_b);
    lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    lcd.drawString("有什么可以帮您?", text_x, text_y + 25);
    
    lcd.setFont(&fonts::efontCN_16_b);
    lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    lcd.drawString("日程 / 备忘 / 问答", text_x, text_y + 60);
    
    int reading_y = text_y + 90;
    lcd.fillCircle(text_x + 6, reading_y + 7, 6, COLOR_TEXT);
    lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    lcd.drawString("今日待办: 3项", text_x + 20, reading_y);
    
    // "生活" 标签
    int label_w = 60;
    int label_h = 28;
    int label_x = detail_x + detail_w - label_w - 4;
    int label_y = detail_y + 4;
    
    lcd.fillRect(label_x, label_y, label_w, label_h, COLOR_BG_DARK);
    lcd.setFont(&fonts::efontCN_16_b);
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(COLOR_TEXT_WHITE, COLOR_BG_DARK);
    lcd.drawString("生活", label_x + label_w / 2, label_y + label_h / 2);
    
    // 保存触摸区域
    _life_btn_x = detail_x;
    _life_btn_y = detail_y;
    _life_btn_w = detail_w;
    _life_btn_h = detail_h;
}
