# USB OTG 音频测试功能说明

## 功能概述

本固件实现了USB OTG功能测试，用于连接USB数字耳机并测试麦克风的可用性。

## 主要特性

1. **自动设备检测**：自动检测连接的USB音频设备
2. **设备识别**：识别USB音频设备类型（耳机、麦克风等）
3. **麦克风测试**：测试麦克风输入功能
4. **实时状态显示**：在M5PaperS3屏幕上显示测试状态

## 使用方法

### 1. 硬件准备
- M5PaperS3设备
- USB Type-C OTG适配器（如果需要）
- USB数字耳机（带麦克风）

### 2. 连接设备
1. 启动M5PaperS3设备
2. 将USB耳机连接到M5PaperS3的USB-C接口
3. 设备会自动检测USB音频设备

### 3. 测试流程

固件启动后会自动运行USB音频测试应用，显示以下状态：

#### 状态1: 初始化
```
USB: Initializing...
```

#### 状态2: 等待设备连接
```
USB: Waiting for device
Connect USB headset ->
```
此时请连接USB耳机。

#### 状态3: 设备已连接
```
USB: Device connected
Audio device OK
Touch to test mic
```
触摸屏幕开始麦克风测试。

#### 状态4: 测试中
```
USB: Testing mic...
Time: 5s Samples: 1000
Touch to stop
```
显示测试时间和采样数。触摸屏幕可以停止测试。

#### 状态5: 错误状态
如果出现错误，会显示错误信息：
```
USB: ERROR
<错误描述>
```

## 技术实现

### USB Host功能
- 使用ESP-IDF的USB Host库
- 支持USB Audio Class (UAC)设备
- 异步事件处理机制

### 关键组件
1. **app_usb_audio.cpp**：USB音频测试应用实现
2. **apps.h**：应用类定义
3. **sdkconfig.defaults**：USB OTG配置

### 配置要求
```
CONFIG_USB_OTG_SUPPORTED=y
CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=256
CONFIG_USB_HOST_HW_BUFFER_BIAS_BALANCED=y
```

## 编译和烧录

### 重新配置项目（如果需要）
```bash
idf.py reconfigure
```

### 编译项目
```bash
idf.py build
```

### 烧录到设备
```bash
idf.py flash monitor
```

## 测试参数

- **测试时长**：最长30秒自动停止
- **采样频率**：根据设备支持情况自适应
- **显示更新频率**：每500ms更新一次

## 调试信息

通过串口监视器可以查看详细的调试信息：
- USB设备连接/断开事件
- 设备VID/PID信息
- 设备类型识别结果
- 测试状态更新

## 注意事项

1. **供电要求**：USB设备需要足够的电流支持，建议连接充电器
2. **兼容性**：支持标准USB Audio Class设备
3. **测试环境**：建议在安静环境下进行麦克风测试
4. **超时保护**：测试会在30秒后自动停止

## 扩展功能

当前实现是基础框架，可以进一步扩展：

1. **实际音频采集**：实现真实的音频流采集
2. **音频播放**：通过耳机播放测试音频
3. **频谱分析**：对麦克风输入进行频谱分析
4. **回声测试**：实现麦克风到耳机的回声测试
5. **录音功能**：将麦克风输入录制到SD卡

## 故障排除

### 问题1: 设备无法识别
- 检查USB连接是否稳定
- 确认USB设备是否为音频设备
- 查看串口输出的设备类型信息

### 问题2: 编译错误
- 确保ESP-IDF版本为5.1或更高
- 运行 `idf.py reconfigure` 重新配置项目
- 检查USB组件是否正确添加到依赖

### 问题3: 测试无响应
- 检查设备是否正确初始化
- 查看串口日志确认状态转换
- 尝试重新连接USB设备

## 技术支持

如有问题，请查看：
- ESP-IDF USB Host文档: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/usb_host.html
- M5PaperS3文档: https://docs.m5stack.com/

## 更新日志

### V0.5 (2025-12-28)
- 初始实现USB OTG音频测试功能
- 支持USB设备自动检测
- 实现基础麦克风测试框架
- 添加屏幕状态显示
