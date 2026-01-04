/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
/**
 * @file app_usb_file.cpp
 * @brief USB File Transfer Information App
 * 
 * This app provides information about file transfer options.
 * USB MSC feature has been removed for safety - use HTTP file server instead.
 */

#include "apps.h"
#include "../hal/hal.h"
#include <mooncake_log.h>
#include <M5Unified.hpp>

// 颜色定义
static constexpr uint32_t COLOR_BG = 0xFFFFFF;
static constexpr uint32_t COLOR_TEXT = 0x000000;
static constexpr uint32_t COLOR_GRAY = 0x808080;
static constexpr uint32_t COLOR_BORDER = 0x333333;
static constexpr uint32_t COLOR_BTN_PRIMARY = 0x333333;
static constexpr uint32_t COLOR_BTN_TEXT = 0xFFFFFF;
static constexpr uint32_t COLOR_SUCCESS = 0x00AA00;

void AppUsbFile::onCreate()
{
    mclog::tagInfo("AppUsbFile", "onCreate");
    
    // 初始化界面
    auto& lcd = GetHAL().display;
    lcd.fillScreen(COLOR_BG);
    
    _need_redraw = true;
}

void AppUsbFile::onRunning()
{
    M5.update();
    
    // 先绘制UI
    if (_need_redraw) {
        drawUI();
        _need_redraw = false;
    }
    
    // 处理返回按钮
    if (GetHAL().wasTouchClickedArea(_back_btn_x, _back_btn_y, 
                                      _back_btn_w, _back_btn_h)) {
        mclog::tagInfo("AppUsbFile", "Back button clicked");
        GetHAL().tone(3000, 50);
        
        // 返回到Home
        auto home_app = std::make_unique<AppHome>();
        int home_id = mooncake::GetMooncake().installApp(std::move(home_app));
        mooncake::GetMooncake().openApp(home_id);
        _need_destroy = true;
    }
    
    // 检查是否需要销毁
    if (_need_destroy) {
        mooncake::GetMooncake().uninstallApp(_app_id);
    }
}

void AppUsbFile::onDestroy()
{
    mclog::tagInfo("AppUsbFile", "onDestroy");
}

void AppUsbFile::drawUI()
{
    auto& lcd = GetHAL().display;
    lcd.fillScreen(COLOR_BG);
    
    int screen_w = lcd.width();
    int screen_h = lcd.height();
    int margin = 30;
    
    // 标题
    lcd.setFont(&fonts::efontCN_24_b);
    lcd.setTextDatum(top_center);
    lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    lcd.drawString("文件传输说明", screen_w / 2, 40);
    
    // 说明内容
    lcd.setFont(&fonts::efontCN_16_b);
    lcd.setTextDatum(top_left);
    
    int text_y = 100;
    int line_height = 30;
    
    // USB MSC 已移除说明
    lcd.setTextColor(COLOR_SUCCESS, COLOR_BG);
    lcd.drawString("✅ 推荐使用 HTTP 文件服务器", margin, text_y);
    text_y += line_height + 10;
    
    // 使用方法
    lcd.setFont(&fonts::efontCN_14);
    lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    
    lcd.drawString("1. 连接到 WiFi 网络", margin + 20, text_y);
    text_y += line_height;
    
    lcd.drawString("2. 进入 WiFi 配置页面", margin + 20, text_y);
    text_y += line_height;
    
    lcd.drawString("3. HTTP 文件服务器会自动启动", margin + 20, text_y);
    text_y += line_height;
    
    lcd.drawString("4. 在浏览器中访问设备 IP 地址", margin + 20, text_y);
    text_y += line_height + 20;
    
    // 优势说明
    lcd.setFont(&fonts::efontCN_16_b);
    lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    lcd.drawString("💡 HTTP 方式的优势:", margin, text_y);
    text_y += line_height + 5;
    
    lcd.setFont(&fonts::efontCN_14);
    lcd.setTextColor(COLOR_GRAY, COLOR_BG);
    lcd.drawString("• 设备功能正常，可同时使用", margin + 20, text_y);
    text_y += line_height;
    
    lcd.drawString("• 无需 USB 线，更方便", margin + 20, text_y);
    text_y += line_height;
    
    lcd.drawString("• 支持完整的文件管理功能", margin + 20, text_y);
    text_y += line_height;
    
    lcd.drawString("• 传输速度已优化（16KB 缓冲）", margin + 20, text_y);
    
    // 返回按钮
    int btn_w = 120;
    int btn_h = 50;
    int btn_y = screen_h - 100;
    
    _back_btn_x = (screen_w - btn_w) / 2;
    _back_btn_y = btn_y;
    _back_btn_w = btn_w;
    _back_btn_h = btn_h;
    
    lcd.fillRect(_back_btn_x, _back_btn_y, _back_btn_w, _back_btn_h, COLOR_BTN_PRIMARY);
    lcd.drawRect(_back_btn_x, _back_btn_y, _back_btn_w, _back_btn_h, COLOR_BORDER);
    
    lcd.setFont(&fonts::efontCN_16_b);
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(COLOR_BTN_TEXT, COLOR_BTN_PRIMARY);
    lcd.drawString("返回", _back_btn_x + _back_btn_w / 2, _back_btn_y + _back_btn_h / 2);
    
    // 应用显示
    GetHAL().display.display();
}
