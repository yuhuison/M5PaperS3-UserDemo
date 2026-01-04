/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <esp_http_server.h>
#include <string>

/**
 * @brief HTTP文件服务器
 * 
 * 提供SD卡文件操作的REST API:
 * - GET  /api/info              - 获取设备信息
 * - GET  /api/list?path=        - 列出目录内容
 * - GET  /api/file?path=        - 下载文件
 * - POST /api/file?path=        - 上传文件
 * - DELETE /api/file?path=      - 删除文件
 * - POST /api/mkdir?path=       - 创建目录
 * - DELETE /api/rmdir?path=     - 递归删除目录
 * - POST /api/upload-batch?dir= - 批量上传文件(multipart/form-data)
 */
class HttpFileServer {
public:
    static HttpFileServer& getInstance();
    
    /**
     * @brief 启动HTTP服务器
     * @param port 端口号，默认80
     * @return true 启动成功
     */
    bool start(uint16_t port = 80);
    
    /**
     * @brief 停止HTTP服务器
     */
    void stop();
    
    /**
     * @brief 检查服务器是否运行中
     */
    bool isRunning() const { return _server != nullptr; }
    
    /**
     * @brief 获取服务器端口
     */
    uint16_t getPort() const { return _port; }
    
    /**
     * @brief 获取服务器URL（基于当前IP）
     */
    std::string getServerUrl() const;

private:
    HttpFileServer() = default;
    ~HttpFileServer();
    
    // 禁止拷贝
    HttpFileServer(const HttpFileServer&) = delete;
    HttpFileServer& operator=(const HttpFileServer&) = delete;
    
    httpd_handle_t _server = nullptr;
    uint16_t _port = 80;
    
    // 注册URI处理器
    void registerUriHandlers();
    
    // URI处理函数
    static esp_err_t handleGetInfo(httpd_req_t* req);
    static esp_err_t handleListDir(httpd_req_t* req);
    static esp_err_t handleGetFile(httpd_req_t* req);
    static esp_err_t handlePostFile(httpd_req_t* req);
    static esp_err_t handleDeleteFile(httpd_req_t* req);
    static esp_err_t handleMkdir(httpd_req_t* req);
    static esp_err_t handleRmdir(httpd_req_t* req);
    static esp_err_t handleUploadBatch(httpd_req_t* req);
    static esp_err_t handleCors(httpd_req_t* req);
    
    // 辅助函数
    static std::string getQueryParam(httpd_req_t* req, const char* key);
    static void sendJsonResponse(httpd_req_t* req, const char* json);
    static void sendErrorResponse(httpd_req_t* req, int code, const char* message);
    static void setCorsHeaders(httpd_req_t* req);
    static bool removeDirectoryRecursive(const std::string& path);
    static bool createDirectoryRecursive(const std::string& path);
};
