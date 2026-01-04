/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "apps.h"
#include <mooncake_log.h>
#include <hal.h>
#include <hal/http_file_server.h>
#include <lgfx/v1/lgfx_fonts.hpp>
#include <esp_wifi.h>
#include <algorithm>
#include <cstdio>
#include <cerrno>

using namespace mooncake;

// 屏幕尺寸
static const int SCREEN_WIDTH = 540;
static const int SCREEN_HEIGHT = 960;


// 颜色定义
static const uint32_t COLOR_BG = 0xFFFFFF;
static const uint32_t COLOR_TEXT = 0x000000;
static const uint32_t COLOR_BORDER = 0xCCCCCC;
static const uint32_t COLOR_SHADOW = 0x333333;
static const uint32_t COLOR_HIGHLIGHT = 0xE0E0E0;
static const uint32_t COLOR_KEY_BG = 0xF0F0F0;
static const uint32_t COLOR_KEY_PRESSED = 0xCCCCCC;
static const uint32_t COLOR_TEXT_WHITE = 0xFFFFFF;
static const uint32_t COLOR_BG_DARK = 0x444444;
static const uint32_t COLOR_GRAY = 0x888888;

// 键盘布局
static const char* KEYBOARD_ROW1 = "1234567890";
static const char* KEYBOARD_ROW2 = "qwertyuiop";
static const char* KEYBOARD_ROW3 = "asdfghjkl";
static const char* KEYBOARD_ROW4 = "zxcvbnm";
static const char* KEYBOARD_ROW1_SHIFT = "!@#$%^&*()";
static const char* KEYBOARD_ROW2_SHIFT = "QWERTYUIOP";
static const char* KEYBOARD_ROW3_SHIFT = "ASDFGHJKL";
static const char* KEYBOARD_ROW4_SHIFT = "ZXCVBNM";

// 键盘参数 - 加大尺寸
static const int KEY_WIDTH = 50;
static const int KEY_HEIGHT = 56;
static const int KEY_MARGIN = 4;
static const int KEYBOARD_START_Y = 480;

// WiFi配置文件路径
static const char* WIFI_CONFIG_PATH = "/sdcard/wifi_config.txt";

void AppWifiConfig::onCreate()
{
    setAppInfo().name = "AppWifiConfig";
    mclog::tagInfo(getAppInfo().name, "onCreate");
    
    GetHAL().display.setRotation(0);
    _state = STATE_SCANNING;
    _password = "";
    _cursor_pos = 0;
    _shift_on = false;
    _selected_wifi = -1;
    _need_destroy = false;
    
    // 确保SD卡已初始化（用于保存WiFi配置）
    if (!GetHAL().isSdCardMounted()) {
        mclog::tagInfo(getAppInfo().name, "SD card not mounted, attempting to initialize...");
        GetHAL().sdCardTest();  // 这会尝试初始化SD卡
    }
    
    // 尝试加载已保存的WiFi配置
    loadWifiConfig();
    
    open();
    
    // 开始扫描
    startScan();
}

void AppWifiConfig::onDestroy()
{
    mclog::tagInfo(getAppInfo().name, "onDestroy");
    
    // 停止HTTP服务器（如果正在运行）
    if (HttpFileServer::getInstance().isRunning()) {
        mclog::tagInfo(getAppInfo().name, "Stopping HTTP server before destroy");
        HttpFileServer::getInstance().stop();
    }
    
    // 重新打开Home App
    int app_id = mooncake::GetMooncake().installApp(std::make_unique<AppHome>());
    mooncake::GetMooncake().openApp(app_id);
}

void AppWifiConfig::onRunning()
{
    // 检查是否需要销毁，卸载自己（会触发onDestroy）
    if (_need_destroy) {
        if (_app_id >= 0) {
            mooncake::GetMooncake().uninstallApp(_app_id);
        }
        return;
    }
    
    static uint32_t last_draw = 0;
    
    switch (_state) {
        case STATE_SCANNING:
            if (GetHAL().millis() - last_draw > 500) {
                drawUI();
                last_draw = GetHAL().millis();
            }
            // 检查扫描是否完成
            if (!GetHAL().getWifiScanResult().ap_list.empty()) {
                // 获取前5个信号最强的
                _wifi_list.clear();
                auto& ap_list = GetHAL().getWifiScanResult().ap_list;
                int count = std::min((int)ap_list.size(), 5);
                for (int i = 0; i < count; i++) {
                    WifiItem item;
                    item.ssid = ap_list[i].second;
                    item.rssi = ap_list[i].first;
                    _wifi_list.push_back(item);
                }
                
                // 检查是否有已保存的WiFi在列表中
                checkSavedWifi();
                
                _state = STATE_SHOW_LIST;
                drawUI();
            }
            break;
            
        case STATE_SHOW_LIST:
            handleWifiListTouch();
            break;
            
        case STATE_INPUT_PASSWORD:
            handlePasswordInputTouch();
            break;
            
        case STATE_CONNECTING:
            // 等待连接结果
            break;
            
        case STATE_CONNECTED:
            handleConnectedTouch();
            break;
            
        case STATE_FAILED:
            {
                auto touch = GetHAL().getTouchDetail();
                if (touch.wasClicked()) {
                    GetHAL().tone(3000, 50);
                    _need_destroy = true;
                }
            }
            break;
            
        case STATE_SERVER_RUNNING:
            handleServerRunningTouch();
            break;
    }
}

void AppWifiConfig::handleWifiListTouch()
{
    auto touch = GetHAL().getTouchDetail();
    if (!touch.wasClicked()) return;
    
    int x = touch.x;
    int y = touch.y;
    
    // 返回按钮
    if (x >= _back_btn_x && x < _back_btn_x + _back_btn_w &&
        y >= _back_btn_y && y < _back_btn_y + _back_btn_h) {
        GetHAL().tone(3000, 50);
        _need_destroy = true;
        return;
    }
    
    // WiFi列表项点击
    int list_start_y = 150;
    int item_height = 70;
    for (size_t i = 0; i < _wifi_list.size(); i++) {
        int item_y = list_start_y + i * item_height;
        if (y >= item_y && y < item_y + item_height && x >= 30 && x < SCREEN_WIDTH - 30) {
            GetHAL().tone(3000, 50);
            _selected_wifi = i;
            
            // 检查是否有保存的密码
            auto it = _saved_passwords.find(_wifi_list[i].ssid);
            if (it != _saved_passwords.end()) {
                _password = it->second;
            } else {
                _password = "";
            }
            _cursor_pos = 0;
            _state = STATE_INPUT_PASSWORD;
            drawUI();
            break;
        }
    }
}

void AppWifiConfig::handlePasswordInputTouch()
{
    auto touch = GetHAL().getTouchDetail();
    if (!touch.wasClicked()) return;
    
    int x = touch.x;
    int y = touch.y;
    
    // 返回按钮
    if (x >= _back_btn_x && x < _back_btn_x + _back_btn_w &&
        y >= _back_btn_y && y < _back_btn_y + _back_btn_h) {
        GetHAL().tone(3000, 50);
        _state = STATE_SHOW_LIST;
        drawUI();
        return;
    }
    
    // 连接按钮
    if (x >= _connect_btn_x && x < _connect_btn_x + _connect_btn_w &&
        y >= _connect_btn_y && y < _connect_btn_y + _connect_btn_h) {
        GetHAL().tone(3000, 50);
        _state = STATE_CONNECTING;
        drawUI();
        connectWifi();
        return;
    }
    
    // 键盘区域
    if (y >= KEYBOARD_START_Y) {
        handleKeyboardTouch(x, y);
    }
}

void AppWifiConfig::drawUI()
{
    auto& lcd = GetHAL().display;
    lcd.setEpdMode(epd_mode_t::epd_quality);
    
    switch (_state) {
        case STATE_SCANNING:
            lcd.fillScreen(COLOR_BG);
            lcd.setFont(&fonts::efontCN_24_b);
            lcd.setTextDatum(middle_center);
            lcd.setTextColor(COLOR_TEXT, COLOR_BG);
            lcd.drawString("正在扫描WiFi...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
            break;
            
        case STATE_SHOW_LIST:
            drawWifiList();
            break;
            
        case STATE_INPUT_PASSWORD:
            drawPasswordInput();
            break;
            
        case STATE_CONNECTING:
            drawConnecting();
            break;
            
        case STATE_CONNECTED:
            drawResult(true);
            break;
            
        case STATE_FAILED:
            drawResult(false);
            break;
            
        case STATE_SERVER_RUNNING:
            drawServerRunning();
            break;
    }
}

void AppWifiConfig::drawWifiList()
{
    auto& lcd = GetHAL().display;
    lcd.fillScreen(COLOR_BG);
    
    // 标题栏
    lcd.fillRect(0, 0, SCREEN_WIDTH, 80, COLOR_BG_DARK);
    lcd.setFont(&fonts::efontCN_24_b);
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(COLOR_TEXT_WHITE, COLOR_BG_DARK);
    lcd.drawString("选择WiFi网络", SCREEN_WIDTH / 2, 40);
    
    // 返回按钮
    _back_btn_x = 20;
    _back_btn_y = 20;
    _back_btn_w = 80;
    _back_btn_h = 40;
    lcd.setFont(&fonts::efontCN_16_b);
    lcd.setTextDatum(middle_left);
    lcd.drawString("< 返回", _back_btn_x, 40);
    
    // WiFi列表
    int list_start_y = 150;
    int item_height = 70;
    
    lcd.setFont(&fonts::efontCN_24_b);
    lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    
    for (size_t i = 0; i < _wifi_list.size(); i++) {
        int item_y = list_start_y + i * item_height;
        
        // 项目背景
        lcd.fillRect(30, item_y, SCREEN_WIDTH - 60, item_height - 10, COLOR_HIGHLIGHT);
        lcd.drawRect(30, item_y, SCREEN_WIDTH - 60, item_height - 10, COLOR_BORDER);
        
        // SSID
        lcd.setTextDatum(middle_left);
        lcd.drawString(_wifi_list[i].ssid.c_str(), 50, item_y + (item_height - 10) / 2);
        
        // 已保存标记
        if (_saved_passwords.find(_wifi_list[i].ssid) != _saved_passwords.end()) {
            lcd.setFont(&fonts::efontCN_14);
            lcd.setTextColor(COLOR_GRAY, COLOR_HIGHLIGHT);
            lcd.drawString("已保存", 50, item_y + (item_height - 10) / 2 + 18);
            lcd.setTextColor(COLOR_TEXT, COLOR_BG);
            lcd.setFont(&fonts::efontCN_24_b);
        }
        
        // 信号强度
        char rssi_str[16];
        snprintf(rssi_str, sizeof(rssi_str), "%d dBm", _wifi_list[i].rssi);
        lcd.setFont(&fonts::efontCN_16_b);
        lcd.setTextDatum(middle_right);
        lcd.drawString(rssi_str, SCREEN_WIDTH - 50, item_y + (item_height - 10) / 2);
        lcd.setFont(&fonts::efontCN_24_b);
    }
    
    // 提示
    lcd.setFont(&fonts::efontCN_16_b);
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(COLOR_GRAY, COLOR_BG);
    lcd.drawString("点击WiFi名称输入密码连接", SCREEN_WIDTH / 2, list_start_y + 5 * item_height + 30);
}

void AppWifiConfig::drawPasswordInput()
{
    auto& lcd = GetHAL().display;
    lcd.fillScreen(COLOR_BG);
    
    // 标题栏
    lcd.fillRect(0, 0, SCREEN_WIDTH, 80, COLOR_BG_DARK);
    lcd.setFont(&fonts::efontCN_24_b);
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(COLOR_TEXT_WHITE, COLOR_BG_DARK);
    
    std::string title = "连接: " + _wifi_list[_selected_wifi].ssid;
    lcd.drawString(title.c_str(), SCREEN_WIDTH / 2, 40);
    
    // 返回按钮
    _back_btn_x = 20;
    _back_btn_y = 20;
    _back_btn_w = 80;
    _back_btn_h = 40;
    lcd.setFont(&fonts::efontCN_16_b);
    lcd.setTextDatum(middle_left);
    lcd.drawString("< 返回", _back_btn_x, 40);
    
    // 密码输入框
    drawPasswordBox();
    
    // 连接按钮
    _connect_btn_x = SCREEN_WIDTH - 130;
    _connect_btn_y = 180;
    _connect_btn_w = 100;
    _connect_btn_h = 45;
    
    lcd.fillRect(_connect_btn_x, _connect_btn_y, _connect_btn_w, _connect_btn_h, COLOR_BG_DARK);
    lcd.setFont(&fonts::efontCN_16_b);
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(COLOR_TEXT_WHITE, COLOR_BG_DARK);
    lcd.drawString("连接", _connect_btn_x + _connect_btn_w / 2, _connect_btn_y + _connect_btn_h / 2);
    
    // 绘制键盘
    drawKeyboard();
}

void AppWifiConfig::drawPasswordBox()
{
    auto& lcd = GetHAL().display;
    
    int input_y = 100;
    lcd.setFont(&fonts::efontCN_16_b);
    lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    lcd.setTextDatum(top_left);
    lcd.drawString("WiFi密码:", 30, input_y);
    
    // 密码显示框
    int pwd_box_y = input_y + 35;
    lcd.fillRect(30, pwd_box_y, SCREEN_WIDTH - 60, 55, COLOR_BG);
    lcd.drawRect(30, pwd_box_y, SCREEN_WIDTH - 60, 55, COLOR_BORDER);
    
    // 显示密码
    lcd.setFont(&fonts::efontCN_24_b);
    lcd.setTextDatum(middle_left);
    std::string display_pwd = _password;
    if (display_pwd.length() > 18) {
        display_pwd = "..." + display_pwd.substr(display_pwd.length() - 15);
    }
    lcd.drawString(display_pwd.c_str(), 40, pwd_box_y + 28);
    
    // 光标
    int cursor_x = 40 + lcd.textWidth(display_pwd.c_str());
    lcd.fillRect(cursor_x, pwd_box_y + 12, 3, 32, COLOR_TEXT);
}

void AppWifiConfig::updatePasswordDisplay()
{
    auto& lcd = GetHAL().display;
    
    // 使用局部刷新
    lcd.setEpdMode(epd_mode_t::epd_fastest);
    
    int input_y = 100;
    int pwd_box_y = input_y + 35;
    
    // 只刷新密码框内部
    lcd.fillRect(31, pwd_box_y + 1, SCREEN_WIDTH - 62, 53, COLOR_BG);
    
    // 显示密码
    lcd.setFont(&fonts::efontCN_24_b);
    lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    lcd.setTextDatum(middle_left);
    std::string display_pwd = _password;
    if (display_pwd.length() > 18) {
        display_pwd = "..." + display_pwd.substr(display_pwd.length() - 15);
    }
    lcd.drawString(display_pwd.c_str(), 40, pwd_box_y + 28);
    
    // 光标
    int cursor_x = 40 + lcd.textWidth(display_pwd.c_str());
    lcd.fillRect(cursor_x, pwd_box_y + 12, 3, 32, COLOR_TEXT);
}

void AppWifiConfig::drawKeyboard()
{
    auto& lcd = GetHAL().display;
    
    const char* row1 = _shift_on ? KEYBOARD_ROW1_SHIFT : KEYBOARD_ROW1;
    const char* row2 = _shift_on ? KEYBOARD_ROW2_SHIFT : KEYBOARD_ROW2;
    const char* row3 = _shift_on ? KEYBOARD_ROW3_SHIFT : KEYBOARD_ROW3;
    const char* row4 = _shift_on ? KEYBOARD_ROW4_SHIFT : KEYBOARD_ROW4;
    
    int start_x = (SCREEN_WIDTH - 10 * (KEY_WIDTH + KEY_MARGIN)) / 2;
    int y = KEYBOARD_START_Y;
    
    // 使用更大的字体
    lcd.setFont(&fonts::efontCN_24_b);
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(COLOR_TEXT, COLOR_KEY_BG);
    
    // 第一行 (数字/符号)
    for (int i = 0; i < 10; i++) {
        int x = start_x + i * (KEY_WIDTH + KEY_MARGIN);
        lcd.fillRect(x, y, KEY_WIDTH, KEY_HEIGHT, COLOR_KEY_BG);
        lcd.drawRect(x, y, KEY_WIDTH, KEY_HEIGHT, COLOR_BORDER);
        char key[2] = {row1[i], 0};
        lcd.drawString(key, x + KEY_WIDTH / 2, y + KEY_HEIGHT / 2);
    }
    
    // 第二行 (qwertyuiop)
    y += KEY_HEIGHT + KEY_MARGIN;
    for (int i = 0; i < 10; i++) {
        int x = start_x + i * (KEY_WIDTH + KEY_MARGIN);
        lcd.fillRect(x, y, KEY_WIDTH, KEY_HEIGHT, COLOR_KEY_BG);
        lcd.drawRect(x, y, KEY_WIDTH, KEY_HEIGHT, COLOR_BORDER);
        char key[2] = {row2[i], 0};
        lcd.drawString(key, x + KEY_WIDTH / 2, y + KEY_HEIGHT / 2);
    }
    
    // 第三行 (asdfghjkl)
    y += KEY_HEIGHT + KEY_MARGIN;
    int row3_start_x = start_x + (KEY_WIDTH + KEY_MARGIN) / 2;
    for (int i = 0; i < 9; i++) {
        int x = row3_start_x + i * (KEY_WIDTH + KEY_MARGIN);
        lcd.fillRect(x, y, KEY_WIDTH, KEY_HEIGHT, COLOR_KEY_BG);
        lcd.drawRect(x, y, KEY_WIDTH, KEY_HEIGHT, COLOR_BORDER);
        char key[2] = {row3[i], 0};
        lcd.drawString(key, x + KEY_WIDTH / 2, y + KEY_HEIGHT / 2);
    }
    
    // 第四行 (Shift + zxcvbnm + Backspace)
    y += KEY_HEIGHT + KEY_MARGIN;
    
    // Shift键
    int shift_w = KEY_WIDTH + 20;
    lcd.fillRect(start_x, y, shift_w, KEY_HEIGHT, _shift_on ? COLOR_BG_DARK : COLOR_KEY_BG);
    lcd.drawRect(start_x, y, shift_w, KEY_HEIGHT, COLOR_BORDER);
    lcd.setTextColor(_shift_on ? COLOR_TEXT_WHITE : COLOR_TEXT, _shift_on ? COLOR_BG_DARK : COLOR_KEY_BG);
    lcd.setFont(&fonts::efontCN_16_b);
    lcd.drawString("Shift", start_x + shift_w / 2, y + KEY_HEIGHT / 2);
    lcd.setTextColor(COLOR_TEXT, COLOR_KEY_BG);
    lcd.setFont(&fonts::efontCN_24_b);
    
    // zxcvbnm
    int row4_start_x = start_x + shift_w + KEY_MARGIN;
    for (int i = 0; i < 7; i++) {
        int x = row4_start_x + i * (KEY_WIDTH + KEY_MARGIN);
        lcd.fillRect(x, y, KEY_WIDTH, KEY_HEIGHT, COLOR_KEY_BG);
        lcd.drawRect(x, y, KEY_WIDTH, KEY_HEIGHT, COLOR_BORDER);
        char key[2] = {row4[i], 0};
        lcd.drawString(key, x + KEY_WIDTH / 2, y + KEY_HEIGHT / 2);
    }
    
    // Backspace键
    int bs_x = row4_start_x + 7 * (KEY_WIDTH + KEY_MARGIN);
    int bs_w = SCREEN_WIDTH - bs_x - start_x;
    lcd.fillRect(bs_x, y, bs_w, KEY_HEIGHT, COLOR_KEY_BG);
    lcd.drawRect(bs_x, y, bs_w, KEY_HEIGHT, COLOR_BORDER);
    lcd.setFont(&fonts::efontCN_16_b);
    lcd.drawString("<-", bs_x + bs_w / 2, y + KEY_HEIGHT / 2);
    lcd.setFont(&fonts::efontCN_24_b);
    
    // 第五行 (空格)
    y += KEY_HEIGHT + KEY_MARGIN;
    int space_x = start_x + 2 * (KEY_WIDTH + KEY_MARGIN);
    int space_w = 6 * (KEY_WIDTH + KEY_MARGIN) - KEY_MARGIN;
    lcd.fillRect(space_x, y, space_w, KEY_HEIGHT, COLOR_KEY_BG);
    lcd.drawRect(space_x, y, space_w, KEY_HEIGHT, COLOR_BORDER);
    lcd.setFont(&fonts::efontCN_16_b);
    lcd.drawString("空格", space_x + space_w / 2, y + KEY_HEIGHT / 2);
}

void AppWifiConfig::handleKeyboardTouch(int x, int y)
{
    const char* row1 = _shift_on ? KEYBOARD_ROW1_SHIFT : KEYBOARD_ROW1;
    const char* row2 = _shift_on ? KEYBOARD_ROW2_SHIFT : KEYBOARD_ROW2;
    const char* row3 = _shift_on ? KEYBOARD_ROW3_SHIFT : KEYBOARD_ROW3;
    const char* row4 = _shift_on ? KEYBOARD_ROW4_SHIFT : KEYBOARD_ROW4;
    
    int start_x = (SCREEN_WIDTH - 10 * (KEY_WIDTH + KEY_MARGIN)) / 2;
    int row_y = KEYBOARD_START_Y;
    
    bool key_pressed = false;
    bool need_full_redraw = false;
    
    // 第一行
    if (y >= row_y && y < row_y + KEY_HEIGHT) {
        for (int i = 0; i < 10; i++) {
            int key_x = start_x + i * (KEY_WIDTH + KEY_MARGIN);
            if (x >= key_x && x < key_x + KEY_WIDTH) {
                GetHAL().tone(4000, 30);
                _password += row1[i];
                key_pressed = true;
                break;
            }
        }
    }
    
    // 第二行
    row_y += KEY_HEIGHT + KEY_MARGIN;
    if (!key_pressed && y >= row_y && y < row_y + KEY_HEIGHT) {
        for (int i = 0; i < 10; i++) {
            int key_x = start_x + i * (KEY_WIDTH + KEY_MARGIN);
            if (x >= key_x && x < key_x + KEY_WIDTH) {
                GetHAL().tone(4000, 30);
                _password += row2[i];
                key_pressed = true;
                break;
            }
        }
    }
    
    // 第三行
    row_y += KEY_HEIGHT + KEY_MARGIN;
    int row3_start_x = start_x + (KEY_WIDTH + KEY_MARGIN) / 2;
    if (!key_pressed && y >= row_y && y < row_y + KEY_HEIGHT) {
        for (int i = 0; i < 9; i++) {
            int key_x = row3_start_x + i * (KEY_WIDTH + KEY_MARGIN);
            if (x >= key_x && x < key_x + KEY_WIDTH) {
                GetHAL().tone(4000, 30);
                _password += row3[i];
                key_pressed = true;
                break;
            }
        }
    }
    
    // 第四行
    row_y += KEY_HEIGHT + KEY_MARGIN;
    int shift_w = KEY_WIDTH + 20;
    
    if (!key_pressed && y >= row_y && y < row_y + KEY_HEIGHT) {
        // Shift键
        if (x >= start_x && x < start_x + shift_w) {
            GetHAL().tone(4000, 30);
            _shift_on = !_shift_on;
            need_full_redraw = true;
        } else {
            // zxcvbnm
            int row4_start_x = start_x + shift_w + KEY_MARGIN;
            for (int i = 0; i < 7; i++) {
                int key_x = row4_start_x + i * (KEY_WIDTH + KEY_MARGIN);
                if (x >= key_x && x < key_x + KEY_WIDTH) {
                    GetHAL().tone(4000, 30);
                    _password += row4[i];
                    key_pressed = true;
                    break;
                }
            }
            
            // Backspace
            if (!key_pressed) {
                int bs_x = row4_start_x + 7 * (KEY_WIDTH + KEY_MARGIN);
                if (x >= bs_x) {
                    GetHAL().tone(4000, 30);
                    if (!_password.empty()) {
                        _password.pop_back();
                    }
                    key_pressed = true;
                }
            }
        }
    }
    
    // 第五行 (空格)
    row_y += KEY_HEIGHT + KEY_MARGIN;
    int space_x = start_x + 2 * (KEY_WIDTH + KEY_MARGIN);
    int space_w = 6 * (KEY_WIDTH + KEY_MARGIN) - KEY_MARGIN;
    if (!key_pressed && !need_full_redraw && y >= row_y && y < row_y + KEY_HEIGHT && 
        x >= space_x && x < space_x + space_w) {
        GetHAL().tone(4000, 30);
        _password += ' ';
        key_pressed = true;
    }
    
    // 更新显示
    if (need_full_redraw) {
        drawPasswordInput();
    } else if (key_pressed) {
        updatePasswordDisplay();
    }
}

void AppWifiConfig::drawConnecting()
{
    auto& lcd = GetHAL().display;
    lcd.fillScreen(COLOR_BG);
    
    lcd.setFont(&fonts::efontCN_24_b);
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    lcd.drawString("正在连接...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 30);
    
    lcd.setFont(&fonts::efontCN_16_b);
    lcd.drawString(_wifi_list[_selected_wifi].ssid.c_str(), SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 20);
}

void AppWifiConfig::drawResult(bool success)
{
    auto& lcd = GetHAL().display;
    lcd.fillScreen(COLOR_BG);
    
    lcd.setFont(&fonts::efontCN_24_b);
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    
    if (success) {
        lcd.drawString("连接成功!", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 100);
        
        // 获取IP地址显示
        lcd.setFont(&fonts::efontCN_16_b);
        std::string server_url = HttpFileServer::getInstance().getServerUrl();
        lcd.drawString(("IP: " + server_url.substr(7)).c_str(), SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 50);
        
        // 开启HTTP服务器按钮
        _server_btn_w = 300;
        _server_btn_h = 60;
        _server_btn_x = (SCREEN_WIDTH - _server_btn_w) / 2;
        _server_btn_y = SCREEN_HEIGHT / 2;
        
        lcd.fillRoundRect(_server_btn_x, _server_btn_y, _server_btn_w, _server_btn_h, 10, COLOR_BG_DARK);
        lcd.setTextColor(COLOR_TEXT_WHITE, COLOR_BG_DARK);
        lcd.setFont(&fonts::efontCN_24_b);
        lcd.drawString("开启HTTP服务器", SCREEN_WIDTH / 2, _server_btn_y + _server_btn_h / 2);
        
        // 返回按钮
        _back_btn_w = 200;
        _back_btn_h = 50;
        _back_btn_x = (SCREEN_WIDTH - _back_btn_w) / 2;
        _back_btn_y = _server_btn_y + _server_btn_h + 30;
        
        lcd.fillRoundRect(_back_btn_x, _back_btn_y, _back_btn_w, _back_btn_h, 10, COLOR_BORDER);
        lcd.setTextColor(COLOR_TEXT, COLOR_BORDER);
        lcd.setFont(&fonts::efontCN_16_b);
        lcd.drawString("返回主页", SCREEN_WIDTH / 2, _back_btn_y + _back_btn_h / 2);
    } else {
        lcd.drawString("连接失败", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 30);
        
        lcd.setFont(&fonts::efontCN_16_b);
        lcd.drawString("点击屏幕返回", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 30);
    }
}

void AppWifiConfig::startScan()
{
    mclog::tagInfo(getAppInfo().name, "Starting WiFi scan");
    GetHAL().wifiScan();
}

void AppWifiConfig::connectWifi()
{
    mclog::tagInfo(getAppInfo().name, "Connecting to WiFi: {}", _wifi_list[_selected_wifi].ssid);
    
    // 初始化WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    
    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, _wifi_list[_selected_wifi].ssid.c_str(), sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, _password.c_str(), sizeof(wifi_config.sta.password) - 1);
    
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_err_t ret = esp_wifi_start();
    
    if (ret == ESP_OK) {
        ret = esp_wifi_connect();
    }
    
    // 等待连接结果
    GetHAL().delay(3000);
    
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        _state = STATE_CONNECTED;
        // 保存WiFi配置
        saveWifiConfig(_wifi_list[_selected_wifi].ssid, _password);
    } else {
        _state = STATE_FAILED;
    }
    
    drawUI();
}

void AppWifiConfig::loadWifiConfig()
{
    mclog::tagInfo(getAppInfo().name, "Loading WiFi config from SD card");
    
    // 检查SD卡是否挂载
    if (!GetHAL().isSdCardMounted()) {
        mclog::tagError(getAppInfo().name, "SD card not mounted, cannot load WiFi config");
        return;
    }
    
    FILE* fp = fopen(WIFI_CONFIG_PATH, "r");
    if (fp == nullptr) {
        mclog::tagInfo(getAppInfo().name, "No saved WiFi config file found (this is normal for first run)");
        return;
    }
    
    mclog::tagInfo(getAppInfo().name, "WiFi config file opened successfully");
    
    char line[256];
    while (fgets(line, sizeof(line), fp) != nullptr) {
        // 格式: SSID|PASSWORD
        char* separator = strchr(line, '|');
        if (separator != nullptr) {
            *separator = '\0';
            std::string ssid = line;
            std::string password = separator + 1;
            
            // 去除换行符
            if (!password.empty() && password.back() == '\n') {
                password.pop_back();
            }
            if (!password.empty() && password.back() == '\r') {
                password.pop_back();
            }
            
            _saved_passwords[ssid] = password;
            mclog::tagInfo(getAppInfo().name, "Loaded saved WiFi: {}", ssid);
        }
    }
    
    fclose(fp);
}

void AppWifiConfig::saveWifiConfig(const std::string& ssid, const std::string& password)
{
    mclog::tagInfo(getAppInfo().name, "Saving WiFi config to SD card: SSID={}", ssid);
    
    // 检查SD卡是否挂载
    mclog::tagInfo(getAppInfo().name, "SD card mounted status: {}", GetHAL().isSdCardMounted() ? "YES" : "NO");
    
    // 先通过 sdCardTest 确保 SD 卡正常工作
    auto& result = GetHAL().getSdCardTestResult();
    GetHAL().sdCardTest();
    mclog::tagInfo(getAppInfo().name, "SD card test result: mounted={}, size={}", 
                   result.is_mounted ? "YES" : "NO", result.size);
    
    if (!result.is_mounted || result.size == "Write Failed") {
        mclog::tagError(getAppInfo().name, "SD card test failed, cannot save WiFi config");
        return;
    }
    
    // 更新内存中的配置
    _saved_passwords[ssid] = password;
    
    // 尝试写入文件
    mclog::tagInfo(getAppInfo().name, "Attempting to open file: {}", WIFI_CONFIG_PATH);
    FILE* fp = fopen(WIFI_CONFIG_PATH, "w");
    if (fp == nullptr) {
        mclog::tagError(getAppInfo().name, "Failed to open file for writing: {} (errno={})", WIFI_CONFIG_PATH, errno);
        // 尝试检查目录是否存在
        FILE* test = fopen("/sdcard/test_write.txt", "w");
        if (test) {
            mclog::tagInfo(getAppInfo().name, "Test file write succeeded, SD card is writable");
            fclose(test);
        } else {
            mclog::tagError(getAppInfo().name, "Test file write also failed (errno={})", errno);
        }
        return;
    }
    
    int count = 0;
    for (const auto& pair : _saved_passwords) {
        fprintf(fp, "%s|%s\n", pair.first.c_str(), pair.second.c_str());
        count++;
    }
    
    fclose(fp);
    mclog::tagInfo(getAppInfo().name, "WiFi config saved successfully, {} entries written", count);
}

void AppWifiConfig::checkSavedWifi()
{
    // 检查是否有已保存的WiFi在扫描列表中，可以用于自动连接
    for (const auto& wifi : _wifi_list) {
        if (_saved_passwords.find(wifi.ssid) != _saved_passwords.end()) {
            mclog::tagInfo(getAppInfo().name, "Found saved WiFi in scan list: {}", wifi.ssid);
        }
    }
}

void AppWifiConfig::handleConnectedTouch()
{
    auto touch = GetHAL().getTouchDetail();
    if (!touch.wasClicked()) return;
    
    int x = touch.x;
    int y = touch.y;
    
    // 开启HTTP服务器按钮
    if (x >= _server_btn_x && x < _server_btn_x + _server_btn_w &&
        y >= _server_btn_y && y < _server_btn_y + _server_btn_h) {
        GetHAL().tone(3000, 50);
        mclog::tagInfo(getAppInfo().name, "Starting HTTP server...");
        
        if (HttpFileServer::getInstance().start(80)) {
            _state = STATE_SERVER_RUNNING;
            drawUI();
        } else {
            mclog::tagError(getAppInfo().name, "Failed to start HTTP server");
        }
        return;
    }
    
    // 返回按钮
    if (x >= _back_btn_x && x < _back_btn_x + _back_btn_w &&
        y >= _back_btn_y && y < _back_btn_y + _back_btn_h) {
        GetHAL().tone(3000, 50);
        _need_destroy = true;
        return;
    }
}

void AppWifiConfig::handleServerRunningTouch()
{
    auto touch = GetHAL().getTouchDetail();
    if (!touch.wasClicked()) return;
    
    int x = touch.x;
    int y = touch.y;
    
    // 停止服务器按钮
    if (x >= _stop_btn_x && x < _stop_btn_x + _stop_btn_w &&
        y >= _stop_btn_y && y < _stop_btn_y + _stop_btn_h) {
        GetHAL().tone(3000, 50);
        mclog::tagInfo(getAppInfo().name, "Stopping HTTP server...");
        HttpFileServer::getInstance().stop();
        _state = STATE_CONNECTED;
        drawUI();
        return;
    }
    
    // 返回按钮
    if (x >= _back_btn_x && x < _back_btn_x + _back_btn_w &&
        y >= _back_btn_y && y < _back_btn_y + _back_btn_h) {
        GetHAL().tone(3000, 50);
        HttpFileServer::getInstance().stop();
        _need_destroy = true;
        return;
    }
}

void AppWifiConfig::drawServerRunning()
{
    auto& lcd = GetHAL().display;
    lcd.fillScreen(COLOR_BG);
    
    // 标题
    lcd.setFont(&fonts::efontCN_24_b);
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    lcd.drawString("HTTP服务器运行中", SCREEN_WIDTH / 2, 100);
    
    // 服务器URL
    std::string server_url = HttpFileServer::getInstance().getServerUrl();
    lcd.setFont(&fonts::efontCN_16_b);
    lcd.drawString(server_url.c_str(), SCREEN_WIDTH / 2, 160);
    
    // API说明
    lcd.setFont(&fonts::efontCN_14);
    lcd.setTextDatum(top_left);
    int info_y = 220;
    int info_x = 40;
    int line_height = 28;
    
    lcd.drawString("可用API:", info_x, info_y);
    info_y += line_height + 5;
    
    lcd.drawString("GET  /api/info        - 获取设备信息", info_x, info_y);
    info_y += line_height;
    lcd.drawString("GET  /api/list?path=  - 列出目录", info_x, info_y);
    info_y += line_height;
    lcd.drawString("GET  /api/file?path=  - 下载文件", info_x, info_y);
    info_y += line_height;
    lcd.drawString("POST /api/file?path=  - 上传文件", info_x, info_y);
    info_y += line_height;
    lcd.drawString("DELETE /api/file?path= - 删除文件", info_x, info_y);
    info_y += line_height;
    lcd.drawString("POST /api/mkdir?path= - 创建目录", info_x, info_y);
    info_y += line_height;
    lcd.drawString("DELETE /api/rmdir?path= - 递归删除目录", info_x, info_y);
    info_y += line_height;
    lcd.drawString("POST /api/upload-batch?dir= - 批量上传", info_x, info_y);
    
    // 停止服务器按钮
    _stop_btn_w = 300;
    _stop_btn_h = 60;
    _stop_btn_x = (SCREEN_WIDTH - _stop_btn_w) / 2;
    _stop_btn_y = 550;
    
    lcd.fillRoundRect(_stop_btn_x, _stop_btn_y, _stop_btn_w, _stop_btn_h, 10, COLOR_BG_DARK);
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(COLOR_TEXT_WHITE, COLOR_BG_DARK);
    lcd.setFont(&fonts::efontCN_24_b);
    lcd.drawString("停止服务器", SCREEN_WIDTH / 2, _stop_btn_y + _stop_btn_h / 2);
    
    // 返回主页按钮
    _back_btn_w = 200;
    _back_btn_h = 50;
    _back_btn_x = (SCREEN_WIDTH - _back_btn_w) / 2;
    _back_btn_y = _stop_btn_y + _stop_btn_h + 30;
    
    lcd.fillRoundRect(_back_btn_x, _back_btn_y, _back_btn_w, _back_btn_h, 10, COLOR_BORDER);
    lcd.setTextColor(COLOR_TEXT, COLOR_BORDER);
    lcd.setFont(&fonts::efontCN_16_b);
    lcd.drawString("返回主页", SCREEN_WIDTH / 2, _back_btn_y + _back_btn_h / 2);
    
    // 提示
    lcd.setTextColor(COLOR_GRAY, COLOR_BG);
    lcd.setFont(&fonts::efontCN_14);
    lcd.drawString("请用浏览器或客户端访问上述地址", SCREEN_WIDTH / 2, _back_btn_y + _back_btn_h + 50);
}
