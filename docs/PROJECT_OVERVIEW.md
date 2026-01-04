# M5PaperS3-UserDemo 项目总览

## 📱 设备信息

### 硬件平台
- **设备型号**: M5Stack PaperS3
- **主控芯片**: ESP32-S3
- **显示屏**: 4.7英寸 E-Paper (电子墨水屏)
- **分辨率**: 540 × 960 像素
- **触摸屏**: 电容式触摸
- **存储**: SD 卡支持
- **其他**: IMU传感器、RTC实时时钟、蜂鸣器、WiFi/蓝牙

### 显示特性
- E-Paper 刷新模式:
  - `epd_quality`: 高质量全刷新（无残影，较慢）
  - `epd_fast`: 快速刷新（有残影，较快）
  - `epd_fastest`: 最快刷新（残影明显）
- 看门狗超时约 5 秒，大图片解码需要喂狗

---

## 🛠 技术栈

### 开发框架
- **ESP-IDF**: v5.1.6 (Espressif IoT Development Framework)
- **构建系统**: CMake + Ninja
- **语言标准**: C++23 (gnu++2b)

### 核心组件
| 组件 | 说明 |
|------|------|
| **M5Unified** | M5Stack 统一硬件抽象库 |
| **M5GFX** | 图形显示库，支持 PNG/JPEG/字体渲染 |
| **Mooncake** | v2.0.0 应用框架，管理 App 生命周期 |
| **mooncake_log** | 日志库，支持格式化输出 |
| **cJSON** | JSON 解析库 |
| **efontCN** | 中文字体支持 (14/16/24 等大小) |

### 字体使用
```cpp
GetHAL().display.setFont(&fonts::efontCN_24_b);  // 24号中文粗体
GetHAL().display.setFont(&fonts::efontCN_16_b);  // 16号中文粗体
GetHAL().display.setFont(&fonts::efontCN_14);    // 14号中文常规
```

---

## 📁 项目结构

```
M5PaperS3-UserDemo/
├── main/
│   ├── main.cpp              # 程序入口
│   ├── CMakeLists.txt        # main 组件配置
│   ├── hal/
│   │   ├── hal.h             # 硬件抽象层头文件
│   │   ├── hal.cpp           # HAL 实现
│   │   ├── http_file_server.h
│   │   └── http_file_server.cpp  # HTTP 文件服务器
│   ├── apps/
│   │   ├── apps.h            # 所有 App 类声明
│   │   ├── app_home.cpp      # 主页 App
│   │   ├── app_bookshelf.cpp # 书架阅读器 App
│   │   ├── app_wifi_config.cpp # WiFi 配置 App
│   │   ├── app_wifi.cpp      # WiFi 测试 App
│   │   ├── app_sd_card.cpp   # SD卡测试 App
│   │   ├── app_buzzer.cpp    # 蜂鸣器测试 App
│   │   ├── app_imu.cpp       # IMU 测试 App
│   │   ├── app_rtc.cpp       # RTC 测试 App
│   │   ├── app_power.cpp     # 电源测试 App
│   │   └── app_usb_audio.cpp # USB音频测试 App
│   └── assets/               # 资源文件
├── components/
│   ├── M5GFX/                # 图形库
│   ├── M5Unified/            # 硬件抽象库
│   ├── mooncake/             # App 框架
│   └── mooncake_log/         # 日志库
├── docs/
│   ├── PROJECT_OVERVIEW.md   # 本文档
│   ├── APP_DEVELOPMENT_GUIDE.md  # App 开发指南
│   ├── BOOK_FORMAT_SPECIFICATION.md  # 书籍格式规范
│   └── HTTP_API_DOCUMENTATION.md  # HTTP API 文档
├── setting-frontend/         # 前端设置页面 (可选)
├── setting-backend/          # 后端服务 (可选)
├── CMakeLists.txt            # 顶层 CMake 配置
├── sdkconfig                 # ESP-IDF 配置
└── partitions.csv            # 分区表
```

---

## 🚀 应用架构

### Mooncake 框架
所有 App 继承自 `mooncake::AppAbility`，需要实现以下生命周期方法：

```cpp
class MyApp : public mooncake::AppAbility {
public:
    void onCreate() override;   // 创建时调用
    void onRunning() override;  // 主循环中持续调用
    void onDestroy() override;  // 销毁前调用
};
```

### App 安装与启动
```cpp
// 安装 App
int app_id = mooncake::GetMooncake().installApp(std::make_unique<MyApp>());

// 打开 App
mooncake::GetMooncake().openApp(app_id);

// 卸载 App (会触发 onDestroy)
mooncake::GetMooncake().uninstallApp(app_id);
```

### HAL 访问
```cpp
// 获取 HAL 单例
auto& hal = GetHAL();

// 使用显示
hal.display.fillScreen(0xFFFFFF);
hal.display.drawString("Hello", 100, 100);

// 获取触摸
auto touch = hal.getTouchDetail();
if (touch.wasClicked()) {
    int x = touch.x;
    int y = touch.y;
}

// 喂狗（长时间操作前）
hal.feedTheDog();
```

---

## 📚 书架 App 功能

### 书籍文件结构
```
/sdcard/books/{book_id}/
├── metadata.json        # 书籍元数据
├── reading_status.json  # 阅读进度
├── cover.png           # 封面 (540×540)
└── sections/
    ├── 001/            # 第1章
    │   ├── 001.png     # 第1页 (540×900)
    │   ├── 002.png
    │   └── ...
    └── 002/            # 第2章
        └── ...
```

### metadata.json 格式
```json
{
  "id": "book_001",
  "title": "书名",
  "author": "作者",
  "sections": [
    {"index": 1, "title": "第一章 标题", "pageCount": 15},
    {"index": 2, "title": "第二章 标题", "pageCount": 12}
  ]
}
```

### 阅读器特性
- **翻页**: 屏幕左半边上一页，右半边下一页
- **刷新模式**: 快速刷新 (fast)，每 5 页全刷新一次
- **底部栏**: 常驻显示目录按钮、进度、返回按钮
- **进度保存**: 自动保存到 reading_status.json

---

## 🌐 HTTP 文件服务器 API

### 可用端点
| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/info` | 获取设备信息 |
| GET | `/api/list?path=/` | 列出目录内容 |
| GET | `/api/file?path=/xxx` | 下载文件 |
| POST | `/api/file?path=/xxx` | 上传单个文件 |
| DELETE | `/api/file?path=/xxx` | 删除文件 |
| POST | `/api/mkdir?path=/xxx` | 创建目录 |
| DELETE | `/api/rmdir?path=/xxx` | 递归删除目录 |
| POST | `/api/upload-batch?dir=/xxx` | 批量上传文件 (multipart) |

### 使用示例
```bash
# 获取设备信息
curl http://192.168.x.x/api/info

# 列出目录
curl "http://192.168.x.x/api/list?path=/books"

# 上传文件
curl -X POST "http://192.168.x.x/api/file?path=/books/test.txt" \
  --data-binary @local_file.txt

# 批量上传
curl -X POST "http://192.168.x.x/api/upload-batch?dir=/books/new_book" \
  -F "file=@cover.png;filename=cover.png" \
  -F "file=@page1.png;filename=sections/001/001.png"

# 递归删除目录
curl -X DELETE "http://192.168.x.x/api/rmdir?path=/books/old_book"
```

---

## 🔧 开发环境配置

### 必需工具
- ESP-IDF v5.1.6
- Python 3.11+
- CMake 3.30+
- Ninja

### VS Code 扩展
- ESP-IDF Extension
- C/C++ Extension

### 构建命令
```bash
# 构建
idf.py build

# 烧录
idf.py flash

# 监视串口
idf.py monitor

# 全部执行
idf.py build flash monitor
```

---

## 📋 已实现功能

### 主页 (AppHome)
- [x] 状态栏显示时间、电池
- [x] 书架卡片（显示当前阅读书籍）
- [x] 推送卡片
- [x] 生活卡片
- [x] WiFi 配置入口

### 书架 (AppBookshelf)
- [x] 书籍列表（分页、按最后阅读排序）
- [x] 封面显示（支持 cover.png 和 COVER.png）
- [x] 阅读进度显示
- [x] 阅读器（章节/页面导航）
- [x] 快速刷新模式（每5页全刷新）
- [x] 底部栏常驻显示
- [x] 目录跳转
- [x] 进度自动保存

### WiFi 配置 (AppWifiConfig)
- [x] WiFi 扫描
- [x] 虚拟键盘密码输入
- [x] 密码保存到 SD 卡
- [x] HTTP 文件服务器

### HTTP 服务器 (HttpFileServer)
- [x] 设备信息 API
- [x] 目录列表 API
- [x] 文件上传/下载
- [x] 文件删除
- [x] 目录创建
- [x] 递归目录删除
- [x] 批量文件上传
- [x] CORS 支持

---

## ⚠️ 注意事项

1. **看门狗超时**: 大图片解码（>100KB PNG）可能触发看门狗，需调用 `feedTheDog()`
2. **内存限制**: ESP32-S3 内存有限，避免一次性加载大文件
3. **E-Paper 刷新**: 全刷新约 1-2 秒，快速刷新有残影
4. **SD 卡路径**: 根路径为 `/sdcard`，所有文件操作基于此
5. **页面图片规格**: 540×900 像素，灰度 PNG，<80KB

---

## 📝 版本历史

- **2025-01**: 初始版本，基础硬件测试 App
- **2026-01**: 添加书架阅读器、HTTP 文件服务器、WiFi 配置
