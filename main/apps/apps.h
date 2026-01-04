/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <mooncake.h>
#include <M5GFX.h>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include "usb/usb_host.h"

/**
 * @brief
 *
 */
class AppPower : public mooncake::AppAbility {
public:
    void onCreate() override;
    void onRunning() override;

private:
    struct UpdateTimeCount_t {
        uint32_t batVoltage = 0;
        uint32_t iconChg    = 0;
        uint32_t lowBat     = 0;
    };
    UpdateTimeCount_t _time_count;
    bool _current_icon_chg = false;

    void update_bat_voltage();
    void update_icon_chg();
    void update_shut_down_button();
    void check_low_battery_power_off();
};

/**
 * @brief
 *
 */
class AppSdCard : public mooncake::AppAbility {
public:
    void onCreate() override;
    void onRunning() override;

private:
    uint32_t _time_count = 0;
};

/**
 * @brief
 *
 */
class AppRtc : public mooncake::AppAbility {
public:
    void onCreate() override;
    void onRunning() override;

private:
    uint32_t _time_update_time_count = 0;
    uint32_t _date_update_time_count = 0;

    void update_date_time();
    void update_sleep_and_wake_up_button();
};

/**
 * @brief
 *
 */
class AppBuzzer : public mooncake::AppAbility {
public:
    void onCreate() override;
    void onRunning() override;
};

/**
 * @brief
 *
 */
class AppImu : public mooncake::AppAbility {
public:
    void onCreate() override;
    void onRunning() override;

private:
    uint32_t _time_count = 0;
};

/**
 * @brief
 *
 */
class AppWifi : public mooncake::AppAbility {
public:
    void onCreate() override;
    void onRunning() override;

    static bool is_wifi_start_scanning();

private:
    enum State_t {
        STATE_IDLE = 0,
        STATE_FIRST_SCAN,
        STATE_SCANNING_RESULT,
    };

    State_t _state       = STATE_IDLE;
    uint32_t _time_count = 0;

    void handle_state_idle();
    void handle_state_first_scan();
    void handle_state_scanning_result();
};

/**
 * @brief USB OTG Audio Test App - Tests USB audio devices (headsets with microphone)
 *
 */
class AppUsbAudio : public mooncake::AppAbility {
public:
    void onCreate() override;
    void onRunning() override;
    void onDestroy() override;

    void handleUsbEvent(const usb_host_client_event_msg_t *event_msg);

private:
    enum State_t {
        STATE_IDLE = 0,
        STATE_USB_INIT,
        STATE_WAITING_DEVICE,
        STATE_DEVICE_CONNECTED,
        STATE_TESTING,
        STATE_ERROR,
    };

    State_t _state = STATE_IDLE;
    
    // USB related
    usb_host_client_handle_t _client_handle = nullptr;
    usb_device_handle_t _device_handle = nullptr;
    uint8_t _device_address = 0;
    bool _device_connected = false;
    bool _is_audio_device = false;
    bool _usb_initialized = false;
    int _device_count = 0;
    uint16_t _device_vid = 0;
    uint16_t _device_pid = 0;
    
    // Test data
    uint32_t _test_start_time = 0;
    uint32_t _test_samples = 0;
    uint16_t _test_max_level = 0;
    std::string _error_msg;

    // State handlers
    void handleIdleState();
    void handleWaitingState();
    void handleDeviceConnected();
    void handleErrorState();
    
    // UI drawing
    void drawMainUI();
    void drawWaitingUI();
    void drawDeviceConnectedUI();
    void drawErrorUI();
    void drawTestingUI();
    void drawButton(int x, int y, int w, int h, const char* label, bool primary);
      
    // USB operations
    void initUsbHost();
    void stopUsbHost();
    void scanForDevices();
    bool openDevice();
    void testMicrophone();
    void updateTestStatus();
};

/**
 * @brief Home Screen App - Main launcher interface
 *
 */
class AppHome : public mooncake::AppAbility {
public:
    void onCreate() override;
    void onRunning() override;

private:
    bool _need_full_refresh = true;
    uint32_t _time_update_count = 0;
    uint32_t _battery_update_count = 0;
    
    // 书架板块触摸区域
    int _bookshelf_btn_x = 0;
    int _bookshelf_btn_y = 0;
    int _bookshelf_btn_w = 0;
    int _bookshelf_btn_h = 0;
    
    // 推送板块触摸区域
    int _push_btn_x = 0;
    int _push_btn_y = 0;
    int _push_btn_w = 0;
    int _push_btn_h = 0;
    
    // 生活板块触摸区域
    int _life_btn_x = 0;
    int _life_btn_y = 0;
    int _life_btn_w = 0;
    int _life_btn_h = 0;
    
    // Book info (mock data for now)
    struct BookInfo {
        std::string title = "葬送的芙莉莲";
        std::string chapter = "黄金乡篇";
        int current_page = 212;
        int total_pages = 360;
        int reading_count = 25;
    };
    BookInfo _current_book;
    
    // UI drawing
    void drawFullUI();
    void drawStatusBar();
    void drawBookshelfCard();
    void drawPushCard();
    void drawLifeCard();
    void drawBookCover(int x, int y, int size);
    void updateTime();
    void updateBattery();
    void drawBottomButtons();
    void handleTouch();
    
    // 底部按钮触摸区域
    int _wifi_btn_x = 0;
    int _wifi_btn_y = 0;
    int _wifi_btn_w = 0;
    int _wifi_btn_h = 0;
    
    // USB File 按钮触摸区域
    int _usb_btn_x = 0;
    int _usb_btn_y = 0;
    int _usb_btn_w = 0;
    int _usb_btn_h = 0;
};

/**
 * @brief Bookshelf App - displays books and reader
 */
class AppBookshelf : public mooncake::AppAbility {
public:
    void onCreate() override;
    void onRunning() override;
    void onDestroy() override;
    
    void setAppId(int id) { _app_id = id; }
    int getAppId() const { return _app_id; }

private:
    int _app_id = -1;
    bool _need_destroy = false;
    bool _need_redraw = true;
    bool _ui_inited = false;
    
    enum State {
        STATE_LOADING,
        STATE_LIST,
        STATE_READING
    };
    State _state = STATE_LOADING;
    
    // 章节信息
    struct SectionInfo {
        int index;
        std::string title;
        int pageCount;
    };
    
    // 图书信息
    struct BookInfo {
        std::string id;
        std::string title;
        std::string author;
        std::string lastReadTime;
        int currentSection;
        int currentPage;
        std::vector<SectionInfo> sections;
        uint8_t* coverData = nullptr;
        size_t coverSize = 0;
    };
    std::vector<BookInfo> _books;
    
    // 列表分页
    int _list_page = 0;
    int _books_per_page = 3;
    int _total_list_pages = 0;
    int _selected_book = -1;
    
    // 阅读状态
    int _reading_section = 0;
    int _reading_page = 0;
    uint8_t* _page_image = nullptr;
    size_t _page_image_size = 0;
    bool _show_toc = false;
    int _page_flip_count = 0;  // 翻页计数，用于控制全刷新
    
    // 触摸区域（列表）
    int _back_btn_x = 0, _back_btn_y = 0, _back_btn_w = 0, _back_btn_h = 0;
    int _prev_list_x = 0, _prev_list_y = 0, _prev_list_w = 0, _prev_list_h = 0;
    int _next_list_x = 0, _next_list_y = 0, _next_list_w = 0, _next_list_h = 0;
    
    // 图书列表UI
    void loadBooks();
    void drawBookList();
    void drawBookItem(int index, int y);
    void handleListTouch();
    
    // 阅读器UI
    void openBook(int bookIndex);
    void loadPage();
    void drawReading(bool fastMode = false);
    void drawBottomBar();
    void drawTOC();
    void handleReadingTouch();
    void saveReadingProgress();
    
    // 翻页
    void nextPage();
    void prevPage();
    void gotoSection(int sectionIndex);
    
    // 工具函数
    void freeBookCovers();
    void freePageImage();
    int getTotalPages();
    int getCurrentGlobalPage();
};

/**
 * @brief USB File Transfer App - High-speed file transfer via USB Serial
 * Uses Web Serial API in browser or Python pyserial
 */
class AppUsbFile : public mooncake::AppAbility {
public:
    void onCreate() override;
    void onRunning() override;
    void onDestroy() override;
    
    void setAppId(int id) { _app_id = id; }
    int getAppId() const { return _app_id; }

private:
    int _app_id = -1;
    bool _need_destroy = false;
    
    enum State {
        STATE_IDLE,
        STATE_RUNNING
    };
    State _state = STATE_IDLE;
    
    bool _need_redraw = true;
    uint32_t _start_time = 0;
    int _transfer_count = 0;
    uint64_t _total_bytes = 0;
    std::string _current_operation;
    int _current_progress = 0;
    
    // 触摸区域
    int _back_btn_x = 0, _back_btn_y = 0, _back_btn_w = 0, _back_btn_h = 0;
    int _start_btn_x = 0, _start_btn_y = 0, _start_btn_w = 0, _start_btn_h = 0;
    int _stop_btn_x = 0, _stop_btn_y = 0, _stop_btn_w = 0, _stop_btn_h = 0;
    
    void handleIdleState();
    void handleRunningState();
    void startServer();
    void stopServer();
    void drawUI();
    void drawIdleUI();
    void drawRunningUI();
};

/**
 * @brief WiFi Configuration App with virtual keyboard
 */
class AppWifiConfig : public mooncake::AppAbility {
public:
    void onCreate() override;
    void onRunning() override;
    void onDestroy() override;
    
    // 设置自己的App ID，用于卸载自己
    void setAppId(int id) { _app_id = id; }
    int getAppId() const { return _app_id; }

private:
    int _app_id = -1;  // 自己的App ID
    
    enum State {
        STATE_SCANNING,
        STATE_SHOW_LIST,
        STATE_INPUT_PASSWORD,
        STATE_CONNECTING,
        STATE_CONNECTED,
        STATE_FAILED,
        STATE_SERVER_RUNNING  // HTTP服务器运行中
    };
    State _state = STATE_SCANNING;
    
    // WiFi列表
    struct WifiItem {
        std::string ssid;
        int rssi;
    };
    std::vector<WifiItem> _wifi_list;
    int _selected_wifi = -1;
    
    // 虚拟键盘
    std::string _password;
    int _cursor_pos = 0;
    bool _shift_on = false;
    bool _show_keyboard = false;
    bool _need_destroy = false;
    
    // 已保存的WiFi密码
    std::map<std::string, std::string> _saved_passwords;
    
    // 触摸区域
    int _back_btn_x = 0, _back_btn_y = 0, _back_btn_w = 0, _back_btn_h = 0;
    int _connect_btn_x = 0, _connect_btn_y = 0, _connect_btn_w = 0, _connect_btn_h = 0;
    int _server_btn_x = 0, _server_btn_y = 0, _server_btn_w = 0, _server_btn_h = 0;
    int _stop_btn_x = 0, _stop_btn_y = 0, _stop_btn_w = 0, _stop_btn_h = 0;
    
    // UI绘制
    void drawUI();
    void drawWifiList();
    void drawPasswordInput();
    void drawPasswordBox();
    void updatePasswordDisplay();
    void drawKeyboard();
    void drawConnecting();
    void drawResult(bool success);
    void drawServerRunning();
    
    // 触摸处理
    void handleWifiListTouch();
    void handlePasswordInputTouch();
    void handleKeyboardTouch(int x, int y);
    void handleConnectedTouch();
    void handleServerRunningTouch();
    
    // WiFi操作
    void startScan();
    void connectWifi();
    
    // SD卡配置
    void loadWifiConfig();
    void saveWifiConfig(const std::string& ssid, const std::string& password);
    void checkSavedWifi();
};

