# M5PaperS3 应用开发指南

本文档记录了M5PaperS3项目的架构设计、关键技术点和常见问题解决方案，帮助后续维护者避免踩坑。

## 目录
1. [项目架构](#项目架构)
2. [Mooncake框架使用](#mooncake框架使用)
3. [App生命周期管理](#app生命周期管理)
4. [E-Paper显示优化](#e-paper显示优化)
5. [SD卡文件系统](#sd卡文件系统)
6. [WiFi配置实现](#wifi配置实现)
7. [常见问题和解决方案](#常见问题和解决方案)

---

## 项目架构

### 技术栈
- **硬件**: M5Stack PaperS3 (ESP32-S3, e-paper 540x960)
- **框架**: ESP-IDF v5.1.6
- **UI库**: M5GFX
- **App管理**: Mooncake v2.0.0
- **字体**: efontCN系列中文字体 (14, 16_b, 24_b)

### 目录结构
```
main/
├── main.cpp              # 入口文件，初始化系统和首页App
├── hal/                  # 硬件抽象层
│   ├── hal.cpp          # 统一的硬件接口封装
│   └── hal.h
└── apps/                 # 所有应用
    ├── apps.h           # App类声明
    ├── app_home.cpp     # 主页App（3卡片设计）
    ├── app_wifi_config.cpp  # WiFi配置App（虚拟键盘）
    ├── app_power.cpp    # 电源管理
    ├── app_sd_card.cpp  # SD卡监控
    └── ...
```

---

## Mooncake框架使用

### 框架概述
Mooncake是一个轻量级的App生命周期管理框架，提供了类似Android的App管理机制。

### 核心概念

#### 1. App生命周期状态
```cpp
enum State_t {
    StateNull = 0,
    StateGoOpen,      // 即将打开
    StateRunning,     // 运行中
    StateGoClose,     // 即将关闭
    StateSleeping,    // 休眠中
};
```

#### 2. 生命周期回调
```cpp
class MyApp : public mooncake::AppAbility {
public:
    void onCreate() override;   // 创建时调用一次
    void onOpen() override;     // 打开时调用
    void onRunning() override;  // 每帧调用（主循环）
    void onClose() override;    // 关闭时调用
    void onSleeping() override; // 休眠状态下每帧调用
    void onDestroy() override;  // 销毁时调用一次
};
```

### 关键API

#### 安装App
```cpp
int app_id = mooncake::GetMooncake().installApp(std::make_unique<MyApp>());
```
- 返回App ID，**必须保存此ID用于后续操作**
- App创建后处于`StateSleeping`状态

#### 打开App
```cpp
mooncake::GetMooncake().openApp(app_id);
```
- 触发`onOpen()`回调
- 切换到`StateRunning`状态

#### 关闭App
```cpp
mooncake::GetMooncake().closeApp(app_id);
```
- 触发`onClose()`回调
- 切换到`StateSleeping`状态
- **注意：不会销毁App！**

#### 卸载App
```cpp
mooncake::GetMooncake().uninstallApp(app_id);
```
- 触发`onDestroy()`回调
- 彻底销毁App，释放内存

---

## App生命周期管理

### ⚠️ 核心要点

#### 1. App必须知道自己的ID才能自我销毁

**问题**: Mooncake框架不提供`getAppId()`方法，App无法获取自己的ID。

**解决方案**: 在App类中添加ID成员变量
```cpp
class AppWifiConfig : public mooncake::AppAbility {
public:
    void setAppId(int id) { _app_id = id; }
    int getAppId() const { return _app_id; }
    
private:
    int _app_id = -1;
};
```

**使用方式**:
```cpp
// 创建时保存指针，以便设置ID
auto wifi_app = std::make_unique<AppWifiConfig>();
AppWifiConfig* wifi_app_ptr = wifi_app.get();
int app_id = mooncake::GetMooncake().installApp(std::move(wifi_app));
wifi_app_ptr->setAppId(app_id);  // 设置App ID
mooncake::GetMooncake().openApp(app_id);
```

#### 2. App返回逻辑的正确实现

**错误做法** ❌:
```cpp
void onRunning() {
    if (need_return) {
        close();  // 只是休眠，不会触发onDestroy
    }
}
```

**正确做法** ✅:
```cpp
void onRunning() {
    if (_need_destroy) {
        if (_app_id >= 0) {
            mooncake::GetMooncake().uninstallApp(_app_id);  // 真正销毁
        }
        return;  // 立即返回，避免继续执行逻辑
    }
    // 正常的App逻辑...
}

void onDestroy() {
    // 在这里创建并打开下一个App（如返回主页）
    int app_id = mooncake::GetMooncake().installApp(std::make_unique<AppHome>());
    mooncake::GetMooncake().openApp(app_id);
}
```

#### 3. App切换流程

**场景**: 从HomeApp打开WiFiConfigApp

```cpp
// 在HomeApp中
void onRunning() {
    if (wifi_button_clicked) {
        // 1. 创建新App并设置ID
        auto wifi_app = std::make_unique<AppWifiConfig>();
        AppWifiConfig* wifi_app_ptr = wifi_app.get();
        int app_id = mooncake::GetMooncake().installApp(std::move(wifi_app));
        wifi_app_ptr->setAppId(app_id);
        
        // 2. 打开新App
        mooncake::GetMooncake().openApp(app_id);
        
        // 3. 关闭当前App（避免继续处理输入）
        close();
        return;
    }
}
```

---

## E-Paper显示优化

### 刷新模式选择

M5PaperS3使用e-paper显示器，刷新速度较慢，需要根据场景选择合适的刷新模式：

```cpp
// 高质量模式 - 完整刷新，无残影（慢）
GetHAL().display.setEpdMode(epd_quality);

// 最快模式 - 部分刷新，有轻微残影（快）
GetHAL().display.setEpdMode(epd_fastest);

// 文本模式 - 文本优化（中等）
GetHAL().display.setEpdMode(epd_text);
```

### 刷新策略

#### 全屏刷新场景
- 页面初始化
- 大范围内容变化
- 需要清除残影

```cpp
void drawUI() {
    auto& lcd = GetHAL().display;
    lcd.setEpdMode(epd_quality);
    lcd.fillScreen(TFT_WHITE);
    // 绘制内容...
    lcd.display();  // 触发刷新
}
```

#### 部分刷新场景
- 虚拟键盘输入（密码显示）
- 状态栏更新
- 动画效果

```cpp
void updatePasswordDisplay() {
    auto& lcd = GetHAL().display;
    lcd.setEpdMode(epd_fastest);  // 使用最快模式
    
    // 只更新密码显示区域
    lcd.fillRect(password_x, password_y, password_w, password_h, TFT_WHITE);
    lcd.drawString(_password.c_str(), password_x, password_y);
    
    lcd.display();  // 部分刷新
}
```

### 中文字体使用

```cpp
// 设置中文字体
lcd.setFont(&fonts::efontCN_24_b);  // 粗体24号
lcd.setFont(&fonts::efontCN_16_b);  // 粗体16号
lcd.setFont(&fonts::efontCN_14);    // 常规14号

// 绘制中文文本
lcd.drawString("WiFi配置", x, y);
```

### 阴影效果实现

```cpp
void drawTextWithShadow(const char* text, int x, int y, uint32_t textColor, uint32_t shadowColor) {
    auto& lcd = GetHAL().display;
    
    // 1. 绘制阴影（偏移2-3像素）
    lcd.setTextColor(shadowColor);
    lcd.drawString(text, x + 3, y + 3);
    
    // 2. 绘制主文本
    lcd.setTextColor(textColor);
    lcd.drawString(text, x, y);
}
```

---

## SD卡文件系统

### ⚠️ 重要配置：FATFS长文件名支持

**默认配置问题**: ESP-IDF的FATFS默认禁用长文件名支持，只支持8.3格式（如`CONFIG~1.TXT`）。

**修改方法**: 编辑`sdkconfig`
```ini
# 启用长文件名支持（推荐HEAP模式）
CONFIG_FATFS_LFN_HEAP=y
CONFIG_FATFS_MAX_LFN=255

# 禁用以下选项
# CONFIG_FATFS_LFN_NONE is not set
# CONFIG_FATFS_LFN_STACK is not set
```

**三种模式对比**:
- `LFN_NONE`: 不支持长文件名（默认）
- `LFN_HEAP`: 缓冲区在堆上分配（推荐，灵活）
- `LFN_STACK`: 缓冲区在栈上分配（快但占用栈空间）

### 文件操作最佳实践

```cpp
#include <stdio.h>
#include <errno.h>

bool saveConfig(const std::string& data) {
    const char* path = "/sdcard/wifi_config.txt";  // 现在可以用长文件名了！
    
    FILE* f = fopen(path, "w");
    if (!f) {
        printf("Failed to open file: %s (errno=%d)\n", path, errno);
        return false;
    }
    
    fprintf(f, "%s", data.c_str());
    fclose(f);
    return true;
}

bool loadConfig(std::string& data) {
    const char* path = "/sdcard/wifi_config.txt";
    
    FILE* f = fopen(path, "r");
    if (!f) {
        printf("Failed to open file: %s (errno=%d)\n", path, errno);
        return false;
    }
    
    char buffer[256];
    data.clear();
    while (fgets(buffer, sizeof(buffer), f)) {
        data += buffer;
    }
    
    fclose(f);
    return true;
}
```

### 常见错误码

- `errno=22 (EINVAL)`: 
  - 通常是文件名格式问题
  - 检查是否启用了长文件名支持
  - 确认路径格式正确（如`/sdcard/`而非`/sdcard`）

- `errno=2 (ENOENT)`: 文件或目录不存在

- `errno=13 (EACCES)`: 权限不足或文件被占用

---

## WiFi配置实现

### 虚拟键盘设计

#### 布局参数
```cpp
static const int SCREEN_WIDTH = 540;
static const int SCREEN_HEIGHT = 960;
static const int KEY_WIDTH = 50;
static const int KEY_HEIGHT = 56;
static const int KEY_MARGIN = 4;
static const int KEYBOARD_START_Y = 480;
```

#### 键盘布局（4行）
```cpp
// 第1行: 数字
"1234567890"

// 第2行: 字母（上）
"qwertyuiop"

// 第3行: 字母（中）
"asdfghjkl"

// 第4行: 字母（下）+ 功能键
"zxcvbnm"
// + Shift键、退格键、空格键
```

#### 触摸处理
```cpp
void handleKeyboardTouch(int x, int y) {
    if (y < KEYBOARD_START_Y) return;  // 不在键盘区域
    
    // 计算按键索引
    int row = (y - KEYBOARD_START_Y) / (KEY_HEIGHT + KEY_MARGIN);
    int col = x / (KEY_WIDTH + KEY_MARGIN);
    
    // 根据row和col获取字符
    // 处理Shift、退格、空格等特殊键
}
```

### WiFi连接流程

```cpp
enum State {
    STATE_SCANNING,       // 扫描WiFi
    STATE_SHOW_LIST,      // 显示WiFi列表
    STATE_INPUT_PASSWORD, // 输入密码
    STATE_CONNECTING,     // 连接中
    STATE_CONNECTED,      // 连接成功
    STATE_FAILED          // 连接失败
};

void onRunning() {
    switch (_state) {
        case STATE_SCANNING:
            // 显示"扫描中..."
            // 等待扫描完成后切换到STATE_SHOW_LIST
            break;
            
        case STATE_SHOW_LIST:
            // 显示WiFi列表
            // 处理点击事件
            break;
            
        case STATE_INPUT_PASSWORD:
            // 显示虚拟键盘
            // 处理输入
            break;
            
        case STATE_CONNECTING:
            // 显示"连接中..."动画
            break;
            
        case STATE_CONNECTED:
        case STATE_FAILED:
            // 显示结果，等待用户点击返回
            break;
    }
}
```

### 密码持久化

```cpp
// 保存格式：每行一个WiFi，SSID和密码用Tab分隔
void saveWifiConfig(const std::string& ssid, const std::string& password) {
    // 1. 加载现有配置
    std::map<std::string, std::string> configs;
    loadWifiConfig();  // 填充configs
    
    // 2. 更新或添加
    configs[ssid] = password;
    
    // 3. 写入文件
    FILE* f = fopen("/sdcard/wifi_config.txt", "w");
    for (auto& pair : configs) {
        fprintf(f, "%s\t%s\n", pair.first.c_str(), pair.second.c_str());
    }
    fclose(f);
}
```

---

## 常见问题和解决方案

### 1. App无法返回主页

**症状**: 点击返回按钮没反应，App继续运行

**原因**: 
- 只调用了`close()`，App进入休眠但未销毁
- App不知道自己的ID，无法调用`uninstallApp()`

**解决方案**: 
- 在App类中添加`_app_id`成员
- 创建App时通过`setAppId()`设置
- 返回时调用`uninstallApp(_app_id)`

### 2. SD卡写入失败 (errno=22)

**症状**: `fopen()`失败，errno=22 (EINVAL)，日志显示"Failed to open file for writing"

**原因**: FATFS长文件名支持被禁用（`CONFIG_FATFS_LFN_NONE=y`），文件名超过8.3格式限制

**详细说明**:
- ESP-IDF默认禁用FATFS长文件名支持
- 8.3格式：`FILENAME.EXT`（文件名≤8字符，扩展名≤3字符）
- `wifi_config.txt` 超过了8.3限制，被视为非法文件名

**解决方案**: 
修改 `sdkconfig` 启用长文件名支持：
```ini
# 启用长文件名支持（HEAP模式）
CONFIG_FATFS_LFN_HEAP=y
CONFIG_FATFS_MAX_LFN=255

# 禁用以下选项
# CONFIG_FATFS_LFN_NONE is not set
```

**三种模式对比**:
- `LFN_NONE`: 不支持长文件名（默认，**会导致errno=22**）
- `LFN_HEAP`: 缓冲区在堆上分配（**推荐**，灵活且内存使用合理）
- `LFN_STACK`: 缓冲区在栈上分配（快但占用栈空间，255字节×2）

**修改后必须重新编译**:
```bash
idf.py fullclean
idf.py build flash
```

### 3. 触摸事件被多个App处理

**症状**: 打开新App后，旧App仍然响应触摸

**原因**: 旧App未调用`close()`，仍在`StateRunning`状态

**解决方案**: 在打开新App后立即调用`close()`
```cpp
int app_id = mooncake::GetMooncake().installApp(std::make_unique<NewApp>());
mooncake::GetMooncake().openApp(app_id);
close();  // 关闭当前App
return;   // 立即返回，避免继续处理
```

### 4. 虚拟键盘输入时全屏刷新

**症状**: 每次按键都触发全屏刷新，很慢且闪烁

**原因**: 使用了`epd_quality`模式

**解决方案**: 输入场景使用`epd_fastest`模式
```cpp
void updatePasswordDisplay() {
    lcd.setEpdMode(epd_fastest);  // 快速部分刷新
    // 只更新密码显示区域
    lcd.display();
}
```

### 5. CMake缓存导致的编译错误

**症状**: 添加新文件后编译找不到，或删除文件后报错

**原因**: CMake使用`GLOB_RECURSE`扫描文件，有缓存

**解决方案**: 
```bash
idf.py fullclean
idf.py build
```

### 6. 中文字体显示方块

**症状**: 中文显示为空白或方块

**原因**: 
- 未设置中文字体
- 使用了不支持中文的字体

**解决方案**: 
```cpp
lcd.setFont(&fonts::efontCN_24_b);  // 使用efontCN系列
```

---

### 7. USB Serial JTAG与SD卡的兼容性

**配置说明**: ESP32-S3支持USB Serial JTAG作为第二串口

**当前配置**:
```ini
CONFIG_ESP_CONSOLE_UART_DEFAULT=y          # 主串口：UART0
CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y  # 副串口：USB Serial JTAG
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED=y
```

**GPIO使用情况**:
- UART0: GPIO43(TX), GPIO44(RX) - 不与SD卡冲突
- USB Serial JTAG: GPIO19(D-), GPIO20(D+) - **可能与某些外设冲突**
- SD卡 SPI: GPIO11(MOSI), GPIO13(MISO), GPIO12(CLK), GPIO47(CS)

**注意事项**:
- M5PaperS3的USB Serial JTAG与SD卡SPI总线**不冲突**
- 如果遇到问题，可以禁用USB Serial JTAG：
  ```ini
  # CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG is not set
  # CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED is not set
  ```

---

## 开发建议

### 1. App结构模板

```cpp
class AppTemplate : public mooncake::AppAbility {
public:
    void setAppId(int id) { _app_id = id; }
    
    void onCreate() override {
        setAppInfo().name = "AppTemplate";
        // 初始化资源
    }
    
    void onRunning() override {
        // 检查是否需要销毁
        if (_need_destroy) {
            if (_app_id >= 0) {
                mooncake::GetMooncake().uninstallApp(_app_id);
            }
            return;
        }
        
        // 正常逻辑
        handleInput();
        updateState();
        drawUI();
    }
    
    void onDestroy() override {
        // 创建下一个App（如果需要）
        int app_id = mooncake::GetMooncake().installApp(std::make_unique<AppHome>());
        mooncake::GetMooncake().openApp(app_id);
    }
    
private:
    int _app_id = -1;
    bool _need_destroy = false;
};
```

### 2. 调试技巧

```cpp
// 使用mclog打印调试信息
#include <mooncake_log.h>

mclog::tagInfo("AppName", "Message: %d", value);
mclog::tagError("AppName", "Error occurred!");
```

### 3. 性能优化

- **减少全屏刷新**: 优先使用`epd_fastest`部分刷新
- **避免频繁分配**: 复用buffer和对象
- **延迟加载**: 在`onOpen()`而非`onCreate()`中加载资源

### 4. 内存管理

- App销毁时自动释放成员变量
- 避免在App间共享裸指针
- 大对象使用`std::unique_ptr`管理

---

## 参考资料

- [Mooncake框架文档](../components/mooncake/README.md)
- [M5GFX文档](../components/M5GFX/README.md)
- [ESP-IDF FATFS配置](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/storage/fatfs.html)
- [M5PaperS3硬件规格](https://docs.m5stack.com/en/core/M5PaperS3)

---

**文档版本**: v1.0  
**最后更新**: 2026-01-03  
**维护者**: GitHub Copilot & Team
