/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "apps.h"
#include <mooncake_log.h>
#include <assets.h>
#include <hal.h>
#include "usb/usb_host.h"
#include <cstring>

using namespace mooncake;

// Button definitions
#define BTN_START_X     300
#define BTN_START_Y     200
#define BTN_START_W     360
#define BTN_START_H     80

#define BTN_SCAN_X      300
#define BTN_SCAN_Y      300
#define BTN_SCAN_W      360
#define BTN_SCAN_H      80

#define BTN_STOP_X      300
#define BTN_STOP_Y      400
#define BTN_STOP_W      360
#define BTN_STOP_H      80

// USB host event handler
static void usb_host_event_callback(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    AppUsbAudio *app = static_cast<AppUsbAudio *>(arg);
    if (app) {
        app->handleUsbEvent(event_msg);
    }
}

// USB library task handle
static TaskHandle_t usb_lib_task_handle = NULL;

// USB library event handler
static void usb_lib_task(void *arg)
{
    while (1) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            mclog::tagInfo("USB", "No clients");
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            mclog::tagInfo("USB", "All devices freed");
        }
    }
}

void AppUsbAudio::onCreate()
{
    setAppInfo().name = "AppUsbAudio";
    mclog::tagInfo(getAppInfo().name, "onCreate");
    
    _state = STATE_IDLE;  // Start in idle state, wait for user action
    _usb_initialized = false;
    open();  // Open by default
}

void AppUsbAudio::onRunning()
{
    switch (_state) {
        case STATE_IDLE:
            handleIdleState();
            break;
            
        case STATE_USB_INIT:
            initUsbHost();
            break;
            
        case STATE_WAITING_DEVICE:
            handleWaitingState();
            break;
            
        case STATE_DEVICE_CONNECTED:
            handleDeviceConnected();
            break;
            
        case STATE_TESTING:
            updateTestStatus();
            break;
            
        case STATE_ERROR:
            handleErrorState();
            break;
    }
}

void AppUsbAudio::handleIdleState()
{
    // Draw initial UI if not done
    static bool ui_drawn = false;
    if (!ui_drawn || GetHAL().isRefreshRequested()) {
        drawMainUI();
        ui_drawn = true;
    }
    
    // Check for Start USB button press
    if (GetHAL().wasTouchClickedArea(BTN_START_X, BTN_START_Y, BTN_START_W, BTN_START_H)) {
        mclog::tagInfo(getAppInfo().name, "Start USB button pressed");
        GetHAL().tone(3000, 50);
        _state = STATE_USB_INIT;
        ui_drawn = false;
    }
}

void AppUsbAudio::handleWaitingState()
{
    // Process USB client events (non-blocking)
    usb_host_client_handle_events(_client_handle, 0);
    
    // Draw waiting UI
    static bool ui_drawn = false;
    if (!ui_drawn || GetHAL().isRefreshRequested()) {
        drawWaitingUI();
        ui_drawn = true;
    }
    
    // Check for scan button press
    if (GetHAL().wasTouchClickedArea(BTN_SCAN_X, BTN_SCAN_Y, BTN_SCAN_W, BTN_SCAN_H)) {
        mclog::tagInfo(getAppInfo().name, "Scan button pressed");
        GetHAL().tone(3000, 50);
        scanForDevices();
        ui_drawn = false;  // Refresh UI after scan
    }
    
    // Check for stop button
    if (GetHAL().wasTouchClickedArea(BTN_STOP_X, BTN_STOP_Y, BTN_STOP_W, BTN_STOP_H)) {
        mclog::tagInfo(getAppInfo().name, "Stop USB button pressed");
        GetHAL().tone(3000, 50);
        stopUsbHost();
        _state = STATE_IDLE;
        ui_drawn = false;
    }
}

void AppUsbAudio::handleDeviceConnected()
{
    static bool ui_drawn = false;
    if (!ui_drawn || GetHAL().isRefreshRequested()) {
        drawDeviceConnectedUI();
        ui_drawn = true;
    }
    
    // Check for test microphone button
    if (_is_audio_device && GetHAL().wasTouchClickedArea(BTN_START_X, BTN_START_Y, BTN_START_W, BTN_START_H)) {
        mclog::tagInfo(getAppInfo().name, "Test mic button pressed");
        GetHAL().tone(3000, 50);
        testMicrophone();
        ui_drawn = false;
    }
    
    // Check for stop button
    if (GetHAL().wasTouchClickedArea(BTN_STOP_X, BTN_STOP_Y, BTN_STOP_W, BTN_STOP_H)) {
        mclog::tagInfo(getAppInfo().name, "Stop button pressed");
        GetHAL().tone(3000, 50);
        stopUsbHost();
        _state = STATE_IDLE;
        ui_drawn = false;
    }
    
    // Process USB events to detect disconnect
    usb_host_client_handle_events(_client_handle, 0);
}

void AppUsbAudio::handleErrorState()
{
    static bool ui_drawn = false;
    if (!ui_drawn || GetHAL().isRefreshRequested()) {
        drawErrorUI();
        ui_drawn = true;
    }
    
    // Check for retry button
    if (GetHAL().wasTouchClickedArea(BTN_START_X, BTN_START_Y, BTN_START_W, BTN_START_H)) {
        mclog::tagInfo(getAppInfo().name, "Retry button pressed");
        GetHAL().tone(3000, 50);
        stopUsbHost();
        _state = STATE_USB_INIT;
        ui_drawn = false;
    }
    
    // Check for back button
    if (GetHAL().wasTouchClickedArea(BTN_STOP_X, BTN_STOP_Y, BTN_STOP_W, BTN_STOP_H)) {
        mclog::tagInfo(getAppInfo().name, "Back button pressed");
        GetHAL().tone(3000, 50);
        stopUsbHost();
        _state = STATE_IDLE;
        ui_drawn = false;
    }
}

void AppUsbAudio::drawMainUI()
{
    GetHAL().display.setEpdMode(epd_mode_t::epd_quality);
    GetHAL().display.fillScreen(TFT_WHITE);
    
    // Title
    GetHAL().display.loadFont(font_montserrat_medium_36);
    GetHAL().display.setTextDatum(top_center);
    GetHAL().display.setTextColor(TFT_BLACK, TFT_WHITE);
    GetHAL().display.drawString("USB OTG Audio Test", GetHAL().display.width() / 2, 50);
    
    // Description
    GetHAL().display.loadFont(font_montserrat_medium_24);
    GetHAL().display.setTextDatum(top_center);
    GetHAL().display.drawString("Test USB headset / microphone", GetHAL().display.width() / 2, 110);
    GetHAL().display.drawString("Connect device to USB-C port", GetHAL().display.width() / 2, 140);
    
    // Start USB button
    drawButton(BTN_START_X, BTN_START_Y, BTN_START_W, BTN_START_H, "Start USB Host", true);
    
    // Status
    GetHAL().display.loadFont(font_montserrat_medium_24);
    GetHAL().display.setTextDatum(top_center);
    GetHAL().display.setTextColor(TFT_DARKGREY, TFT_WHITE);
    GetHAL().display.drawString("Status: Ready", GetHAL().display.width() / 2, 450);
    
    GetHAL().display.display();
}

void AppUsbAudio::drawWaitingUI()
{
    GetHAL().display.setEpdMode(epd_mode_t::epd_quality);
    GetHAL().display.fillScreen(TFT_WHITE);
    
    // Title
    GetHAL().display.loadFont(font_montserrat_medium_36);
    GetHAL().display.setTextDatum(top_center);
    GetHAL().display.setTextColor(TFT_BLACK, TFT_WHITE);
    GetHAL().display.drawString("USB Host Active", GetHAL().display.width() / 2, 50);
    
    // Status
    GetHAL().display.loadFont(font_montserrat_medium_24);
    GetHAL().display.drawString("Waiting for USB device...", GetHAL().display.width() / 2, 110);
    GetHAL().display.drawString("Connect headset to USB-C port", GetHAL().display.width() / 2, 140);
    
    // Scan button
    drawButton(BTN_SCAN_X, BTN_SCAN_Y, BTN_SCAN_W, BTN_SCAN_H, "Scan Devices", true);
    
    // Stop button
    drawButton(BTN_STOP_X, BTN_STOP_Y, BTN_STOP_W, BTN_STOP_H, "Stop USB Host", false);
    
    // Device count
    GetHAL().display.setTextDatum(top_center);
    GetHAL().display.setTextColor(TFT_DARKGREY, TFT_WHITE);
    std::string status = fmt::format("Devices found: {}", _device_count);
    GetHAL().display.drawString(status.c_str(), GetHAL().display.width() / 2, 220);
    
    GetHAL().display.display();
}

void AppUsbAudio::drawDeviceConnectedUI()
{
    GetHAL().display.setEpdMode(epd_mode_t::epd_quality);
    GetHAL().display.fillScreen(TFT_WHITE);
    
    // Title
    GetHAL().display.loadFont(font_montserrat_medium_36);
    GetHAL().display.setTextDatum(top_center);
    GetHAL().display.setTextColor(TFT_BLACK, TFT_WHITE);
    GetHAL().display.drawString("Device Connected!", GetHAL().display.width() / 2, 50);
    
    // Device info
    GetHAL().display.loadFont(font_montserrat_medium_24);
    std::string vid_pid = fmt::format("VID: 0x{:04X}  PID: 0x{:04X}", _device_vid, _device_pid);
    GetHAL().display.drawString(vid_pid.c_str(), GetHAL().display.width() / 2, 110);
    
    if (_is_audio_device) {
        GetHAL().display.setTextColor(TFT_DARKGREEN, TFT_WHITE);
        GetHAL().display.drawString("Audio Device Detected", GetHAL().display.width() / 2, 150);
        GetHAL().display.setTextColor(TFT_BLACK, TFT_WHITE);
        
        // Test mic button
        drawButton(BTN_START_X, BTN_START_Y, BTN_START_W, BTN_START_H, "Test Microphone", true);
    } else {
        GetHAL().display.setTextColor(TFT_ORANGE, TFT_WHITE);
        GetHAL().display.drawString("Not an Audio Device", GetHAL().display.width() / 2, 150);
        GetHAL().display.setTextColor(TFT_BLACK, TFT_WHITE);
    }
    
    // Stop button
    drawButton(BTN_STOP_X, BTN_STOP_Y, BTN_STOP_W, BTN_STOP_H, "Disconnect", false);
    
    GetHAL().display.display();
}

void AppUsbAudio::drawErrorUI()
{
    GetHAL().display.setEpdMode(epd_mode_t::epd_quality);
    GetHAL().display.fillScreen(TFT_WHITE);
    
    // Title
    GetHAL().display.loadFont(font_montserrat_medium_36);
    GetHAL().display.setTextDatum(top_center);
    GetHAL().display.setTextColor(TFT_RED, TFT_WHITE);
    GetHAL().display.drawString("Error!", GetHAL().display.width() / 2, 50);
    
    // Error message
    GetHAL().display.loadFont(font_montserrat_medium_24);
    GetHAL().display.setTextColor(TFT_BLACK, TFT_WHITE);
    GetHAL().display.drawString(_error_msg.c_str(), GetHAL().display.width() / 2, 130);
    
    // Retry button
    drawButton(BTN_START_X, BTN_START_Y, BTN_START_W, BTN_START_H, "Retry", true);
    
    // Back button
    drawButton(BTN_STOP_X, BTN_STOP_Y, BTN_STOP_W, BTN_STOP_H, "Back", false);
    
    GetHAL().display.display();
}

void AppUsbAudio::drawButton(int x, int y, int w, int h, const char* label, bool primary)
{
    if (primary) {
        GetHAL().display.fillRoundRect(x, y, w, h, 10, TFT_BLACK);
        GetHAL().display.setTextColor(TFT_WHITE, TFT_BLACK);
    } else {
        GetHAL().display.drawRoundRect(x, y, w, h, 10, TFT_BLACK);
        GetHAL().display.drawRoundRect(x+1, y+1, w-2, h-2, 9, TFT_BLACK);
        GetHAL().display.setTextColor(TFT_BLACK, TFT_WHITE);
    }
    
    GetHAL().display.loadFont(font_montserrat_medium_24);
    GetHAL().display.setTextDatum(middle_center);
    GetHAL().display.drawString(label, x + w/2, y + h/2);
}

void AppUsbAudio::initUsbHost()
{
    mclog::tagInfo(getAppInfo().name, "Initializing USB Host...");
    
    // Show initializing status
    GetHAL().display.setEpdMode(epd_mode_t::epd_fastest);
    GetHAL().display.fillScreen(TFT_WHITE);
    GetHAL().display.loadFont(font_montserrat_medium_36);
    GetHAL().display.setTextDatum(middle_center);
    GetHAL().display.setTextColor(TFT_BLACK, TFT_WHITE);
    GetHAL().display.drawString("Initializing USB...", GetHAL().display.width() / 2, GetHAL().display.height() / 2);
    GetHAL().display.display();
    
    // Install USB Host driver
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {  // ESP_ERR_INVALID_STATE means already installed
        mclog::tagError(getAppInfo().name, "USB Host install failed: %d", err);
        _state = STATE_ERROR;
        _error_msg = "USB Host install failed";
        return;
    }
    
    // Create USB library task if not already created
    if (usb_lib_task_handle == NULL) {
        xTaskCreate(usb_lib_task, "usb_lib", 4096, NULL, 10, &usb_lib_task_handle);
    }
    
    // Register USB Host client
    const usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = usb_host_event_callback,
            .callback_arg = this,
        },
    };
    
    err = usb_host_client_register(&client_config, &_client_handle);
    if (err != ESP_OK) {
        mclog::tagError(getAppInfo().name, "USB Host client register failed: %d", err);
        _state = STATE_ERROR;
        _error_msg = "Client register failed";
        return;
    }
    
    mclog::tagInfo(getAppInfo().name, "USB Host initialized successfully");
    _usb_initialized = true;
    _device_count = 0;
    _state = STATE_WAITING_DEVICE;
}

void AppUsbAudio::stopUsbHost()
{
    mclog::tagInfo(getAppInfo().name, "Stopping USB Host...");
    
    if (_device_handle) {
        usb_host_device_close(_client_handle, _device_handle);
        _device_handle = nullptr;
    }
    
    if (_client_handle) {
        usb_host_client_deregister(_client_handle);
        _client_handle = nullptr;
    }
    
    _usb_initialized = false;
    _device_connected = false;
    _device_address = 0;
    _device_count = 0;
}

void AppUsbAudio::scanForDevices()
{
    mclog::tagInfo(getAppInfo().name, "Scanning for USB devices...");
    
    int num_devices = 0;
    uint8_t dev_addr_list[10];
    esp_err_t err = usb_host_device_addr_list_fill(sizeof(dev_addr_list), dev_addr_list, &num_devices);
    
    _device_count = num_devices;
    
    if (err == ESP_OK && num_devices > 0) {
        mclog::tagInfo(getAppInfo().name, "Found %d USB device(s)", num_devices);
        
        // Log all device addresses
        for (int i = 0; i < num_devices; i++) {
            mclog::tagInfo(getAppInfo().name, "Device %d at address: %d", i, dev_addr_list[i]);
        }
        
        // Try to open the first device
        _device_address = dev_addr_list[0];
        _device_connected = true;
        if (openDevice()) {
            _state = STATE_DEVICE_CONNECTED;
        }
    } else {
        mclog::tagInfo(getAppInfo().name, "No USB devices found");
    }
}

void AppUsbAudio::handleUsbEvent(const usb_host_client_event_msg_t *event_msg)
{
    switch (event_msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
            mclog::tagInfo(getAppInfo().name, "New device connected, address: %d", event_msg->new_dev.address);
            _device_address = event_msg->new_dev.address;
            _device_connected = true;
            
            // Open device and get info
            if (openDevice()) {
                _state = STATE_DEVICE_CONNECTED;
            } else {
                _state = STATE_ERROR;
                _error_msg = "Failed to open device";
            }
            break;
            
        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            mclog::tagInfo(getAppInfo().name, "Device disconnected");
            if (_device_handle) {
                usb_host_device_close(_client_handle, _device_handle);
                _device_handle = nullptr;
            }
            _device_connected = false;
            _device_address = 0;
            _state = STATE_WAITING_DEVICE;
            break;
            
        default:
            break;
    }
}

bool AppUsbAudio::openDevice()
{
    esp_err_t err = usb_host_device_open(_client_handle, _device_address, &_device_handle);
    if (err != ESP_OK) {
        mclog::tagError(getAppInfo().name, "Failed to open device: %d", err);
        return false;
    }
    
    // Get device descriptor
    const usb_device_desc_t *dev_desc;
    err = usb_host_get_device_descriptor(_device_handle, &dev_desc);
    if (err != ESP_OK) {
        mclog::tagError(getAppInfo().name, "Failed to get device descriptor: %d", err);
        usb_host_device_close(_client_handle, _device_handle);
        return false;
    }
    
    // Save VID/PID for display
    _device_vid = dev_desc->idVendor;
    _device_pid = dev_desc->idProduct;
    
    mclog::tagInfo(getAppInfo().name, "Device VID: 0x%04X, PID: 0x%04X", 
                   dev_desc->idVendor, dev_desc->idProduct);
    
    // Check if it's an audio device (Class 1 = Audio)
    if (dev_desc->bDeviceClass == USB_CLASS_AUDIO || dev_desc->bDeviceClass == 0) {
        mclog::tagInfo(getAppInfo().name, "Audio device detected");
        _is_audio_device = true;
    } else {
        mclog::tagWarn(getAppInfo().name, "Not an audio device, class: 0x%02X", dev_desc->bDeviceClass);
        _is_audio_device = false;
    }
    
    return true;
}

void AppUsbAudio::testMicrophone()
{
    mclog::tagInfo(getAppInfo().name, "Starting microphone test");
    
    if (!_is_audio_device) {
        _state = STATE_ERROR;
        _error_msg = "Not an audio device";
        return;
    }
    
    // Initialize test parameters
    _test_samples = 0;
    _test_max_level = 0;
    _test_start_time = GetHAL().millis();
    _state = STATE_TESTING;
    
    // Note: Actual audio streaming would require UAC driver implementation
    // This is a simplified test that checks device presence and configuration
    mclog::tagInfo(getAppInfo().name, "Microphone test initiated");
}

void AppUsbAudio::updateTestStatus()
{
    static bool ui_drawn = false;
    if (!ui_drawn || GetHAL().isRefreshRequested()) {
        drawTestingUI();
        ui_drawn = true;
    }
    
    uint32_t elapsed = GetHAL().millis() - _test_start_time;
    
    // Simulate test data (in real implementation, would read from microphone)
    _test_samples++;
    
    // Update display every second
    static uint32_t last_ui_update = 0;
    if (GetHAL().millis() - last_ui_update > 1000) {
        ui_drawn = false;
        last_ui_update = GetHAL().millis();
    }
    
    // Check for touch to stop test
    if (GetHAL().wasTouchClickedArea(BTN_STOP_X, BTN_STOP_Y, BTN_STOP_W, BTN_STOP_H)) {
        mclog::tagInfo(getAppInfo().name, "Test stopped by user");
        _state = STATE_DEVICE_CONNECTED;
        GetHAL().tone(3000, 100);
        ui_drawn = false;
    }
    
    // Auto stop after 30 seconds
    if (elapsed > 30000) {
        mclog::tagInfo(getAppInfo().name, "Test completed");
        _state = STATE_DEVICE_CONNECTED;
        ui_drawn = false;
    }
    
    // Process USB events
    usb_host_client_handle_events(_client_handle, 0);
}

void AppUsbAudio::drawTestingUI()
{
    GetHAL().display.setEpdMode(epd_mode_t::epd_fastest);
    GetHAL().display.fillScreen(TFT_WHITE);
    
    // Title
    GetHAL().display.loadFont(font_montserrat_medium_36);
    GetHAL().display.setTextDatum(top_center);
    GetHAL().display.setTextColor(TFT_BLACK, TFT_WHITE);
    GetHAL().display.drawString("Testing Microphone", GetHAL().display.width() / 2, 50);
    
    // Status info
    GetHAL().display.loadFont(font_montserrat_medium_24);
    uint32_t elapsed = (GetHAL().millis() - _test_start_time) / 1000;
    std::string time_str = fmt::format("Time: {}s", elapsed);
    std::string samples_str = fmt::format("Samples: {}", _test_samples);
    
    GetHAL().display.drawString(time_str.c_str(), GetHAL().display.width() / 2, 150);
    GetHAL().display.drawString(samples_str.c_str(), GetHAL().display.width() / 2, 190);
    
    // Note about UAC
    GetHAL().display.setTextColor(TFT_DARKGREY, TFT_WHITE);
    GetHAL().display.drawString("(Full UAC driver not implemented)", GetHAL().display.width() / 2, 280);
    GetHAL().display.drawString("Device detected successfully!", GetHAL().display.width() / 2, 310);
    
    // Stop button
    drawButton(BTN_STOP_X, BTN_STOP_Y, BTN_STOP_W, BTN_STOP_H, "Stop Test", false);
    
    GetHAL().display.display();
}

void AppUsbAudio::onDestroy()
{
    stopUsbHost();
    
    // Stop and delete USB lib task if running
    if (usb_lib_task_handle != NULL) {
        vTaskDelete(usb_lib_task_handle);
        usb_lib_task_handle = NULL;
    }
    
    usb_host_uninstall();
}
