/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "http_file_server.h"
#include <mooncake_log.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_vfs_fat.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdio>

static const char* TAG = "HttpFileServer";

// 文件读写缓冲区大小 - 增大到 16KB 可以显著提高传输速度
// 注意：此值受 SPI max_transfer_sz 限制，但可以减少系统调用次数
static const size_t FILE_BUFFER_SIZE = 16384;

// SD卡根路径
static const char* SD_ROOT = "/sdcard";

HttpFileServer& HttpFileServer::getInstance()
{
    static HttpFileServer instance;
    return instance;
}

HttpFileServer::~HttpFileServer()
{
    stop();
}

bool HttpFileServer::start(uint16_t port)
{
    if (_server != nullptr) {
        mclog::tagWarn(TAG, "Server already running on port {}", _port);
        return true;
    }
    
    _port = port;
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 16;
    config.stack_size = 8192;
    
    mclog::tagInfo(TAG, "Starting HTTP server on port {}", port);
    
    esp_err_t ret = httpd_start(&_server, &config);
    if (ret != ESP_OK) {
        mclog::tagError(TAG, "Failed to start HTTP server: {}", esp_err_to_name(ret));
        return false;
    }
    
    registerUriHandlers();
    
    mclog::tagInfo(TAG, "HTTP server started successfully");
    mclog::tagInfo(TAG, "Server URL: {}", getServerUrl());
    
    return true;
}

void HttpFileServer::stop()
{
    if (_server != nullptr) {
        mclog::tagInfo(TAG, "Stopping HTTP server");
        httpd_stop(_server);
        _server = nullptr;
    }
}

std::string HttpFileServer::getServerUrl() const
{
    esp_netif_ip_info_t ip_info;
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
        
        if (_port == 80) {
            return std::string("http://") + ip_str;
        } else {
            return std::string("http://") + ip_str + ":" + std::to_string(_port);
        }
    }
    
    return "http://unknown";
}

void HttpFileServer::registerUriHandlers()
{
    // CORS预检请求
    httpd_uri_t cors_options = {
        .uri = "/api/*",
        .method = HTTP_OPTIONS,
        .handler = handleCors,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(_server, &cors_options);
    
    // GET /api/info - 获取设备信息
    httpd_uri_t get_info = {
        .uri = "/api/info",
        .method = HTTP_GET,
        .handler = handleGetInfo,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(_server, &get_info);
    
    // GET /api/list - 列出目录
    httpd_uri_t get_list = {
        .uri = "/api/list",
        .method = HTTP_GET,
        .handler = handleListDir,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(_server, &get_list);
    
    // GET /api/file - 下载文件
    httpd_uri_t get_file = {
        .uri = "/api/file",
        .method = HTTP_GET,
        .handler = handleGetFile,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(_server, &get_file);
    
    // POST /api/file - 上传文件
    httpd_uri_t post_file = {
        .uri = "/api/file",
        .method = HTTP_POST,
        .handler = handlePostFile,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(_server, &post_file);
    
    // DELETE /api/file - 删除文件
    httpd_uri_t delete_file = {
        .uri = "/api/file",
        .method = HTTP_DELETE,
        .handler = handleDeleteFile,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(_server, &delete_file);
    
    // POST /api/mkdir - 创建目录
    httpd_uri_t post_mkdir = {
        .uri = "/api/mkdir",
        .method = HTTP_POST,
        .handler = handleMkdir,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(_server, &post_mkdir);
    
    // DELETE /api/rmdir - 递归删除目录
    httpd_uri_t delete_rmdir = {
        .uri = "/api/rmdir",
        .method = HTTP_DELETE,
        .handler = handleRmdir,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(_server, &delete_rmdir);
    
    // POST /api/upload-batch - 批量上传文件
    httpd_uri_t post_upload_batch = {
        .uri = "/api/upload-batch",
        .method = HTTP_POST,
        .handler = handleUploadBatch,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(_server, &post_upload_batch);
    
    mclog::tagInfo(TAG, "URI handlers registered");
}

// CORS处理
esp_err_t HttpFileServer::handleCors(httpd_req_t* req)
{
    setCorsHeaders(req);
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
}

void HttpFileServer::setCorsHeaders(httpd_req_t* req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
}

std::string HttpFileServer::getQueryParam(httpd_req_t* req, const char* key)
{
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len <= 1) {
        return "";
    }
    
    char* buf = new char[buf_len];
    if (httpd_req_get_url_query_str(req, buf, buf_len) != ESP_OK) {
        delete[] buf;
        return "";
    }
    
    char value[256] = {0};
    if (httpd_query_key_value(buf, key, value, sizeof(value)) != ESP_OK) {
        delete[] buf;
        return "";
    }
    
    delete[] buf;
    
    // URL解码
    std::string decoded;
    for (size_t i = 0; i < strlen(value); i++) {
        if (value[i] == '%' && i + 2 < strlen(value)) {
            char hex[3] = {value[i + 1], value[i + 2], 0};
            decoded += (char)strtol(hex, nullptr, 16);
            i += 2;
        } else if (value[i] == '+') {
            decoded += ' ';
        } else {
            decoded += value[i];
        }
    }
    
    return decoded;
}

void HttpFileServer::sendJsonResponse(httpd_req_t* req, const char* json)
{
    setCorsHeaders(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
}

void HttpFileServer::sendErrorResponse(httpd_req_t* req, int code, const char* message)
{
    setCorsHeaders(req);
    httpd_resp_set_type(req, "application/json");
    
    char json[256];
    snprintf(json, sizeof(json), "{\"error\":true,\"code\":%d,\"message\":\"%s\"}", code, message);
    
    if (code == 404) {
        httpd_resp_set_status(req, "404 Not Found");
    } else if (code == 400) {
        httpd_resp_set_status(req, "400 Bad Request");
    } else if (code == 500) {
        httpd_resp_set_status(req, "500 Internal Server Error");
    }
    
    httpd_resp_send(req, json, strlen(json));
}

// GET /api/info
esp_err_t HttpFileServer::handleGetInfo(httpd_req_t* req)
{
    mclog::tagInfo(TAG, "GET /api/info");
    
    // 获取IP地址
    esp_netif_ip_info_t ip_info;
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    char ip_str[16] = "unknown";
    
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
    }
    
    // 获取WiFi信息
    wifi_ap_record_t ap_info;
    char ssid[33] = "unknown";
    int rssi = 0;
    
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        strncpy(ssid, (char*)ap_info.ssid, sizeof(ssid) - 1);
        rssi = ap_info.rssi;
    }
    
    // 获取SD卡空间信息（使用esp_vfs_fat API）
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    
    FATFS* fs;
    DWORD fre_clust;
    if (f_getfree("0:", &fre_clust, &fs) == FR_OK) {
        total_bytes = (uint64_t)(fs->n_fatent - 2) * fs->csize * 512;
        free_bytes = (uint64_t)fre_clust * fs->csize * 512;
    }
    
    char json[512];
    snprintf(json, sizeof(json),
        "{"
        "\"device\":\"M5PaperS3\","
        "\"ip\":\"%s\","
        "\"wifi\":{\"ssid\":\"%s\",\"rssi\":%d},"
        "\"storage\":{\"total\":%llu,\"free\":%llu,\"used\":%llu}"
        "}",
        ip_str, ssid, rssi,
        total_bytes, free_bytes, total_bytes - free_bytes
    );
    
    sendJsonResponse(req, json);
    return ESP_OK;
}

// GET /api/list?path=/path/to/dir
esp_err_t HttpFileServer::handleListDir(httpd_req_t* req)
{
    std::string path = getQueryParam(req, "path");
    if (path.empty()) {
        path = "/";
    }
    
    // 构建完整路径
    std::string full_path = SD_ROOT + path;
    mclog::tagInfo(TAG, "GET /api/list path={}", full_path);
    
    DIR* dir = opendir(full_path.c_str());
    if (dir == nullptr) {
        sendErrorResponse(req, 404, "Directory not found");
        return ESP_OK;
    }
    
    // 构建JSON响应
    std::string json = "{\"path\":\"" + path + "\",\"items\":[";
    
    struct dirent* entry;
    bool first = true;
    
    while ((entry = readdir(dir)) != nullptr) {
        // 跳过 . 和 ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        if (!first) {
            json += ",";
        }
        first = false;
        
        // 获取文件信息
        std::string item_path = full_path + "/" + entry->d_name;
        struct stat st;
        stat(item_path.c_str(), &st);
        
        bool is_dir = S_ISDIR(st.st_mode);
        
        json += "{";
        json += "\"name\":\"" + std::string(entry->d_name) + "\",";
        json += "\"type\":\"" + std::string(is_dir ? "directory" : "file") + "\"";
        
        if (!is_dir) {
            json += ",\"size\":" + std::to_string(st.st_size);
        }
        
        json += "}";
    }
    
    closedir(dir);
    
    json += "]}";
    
    sendJsonResponse(req, json.c_str());
    return ESP_OK;
}

// GET /api/file?path=/path/to/file
esp_err_t HttpFileServer::handleGetFile(httpd_req_t* req)
{
    std::string path = getQueryParam(req, "path");
    if (path.empty()) {
        sendErrorResponse(req, 400, "Path parameter required");
        return ESP_OK;
    }
    
    std::string full_path = SD_ROOT + path;
    mclog::tagInfo(TAG, "GET /api/file path={}", full_path);
    
    FILE* fp = fopen(full_path.c_str(), "rb");
    if (fp == nullptr) {
        sendErrorResponse(req, 404, "File not found");
        return ESP_OK;
    }
    
    // 设置响应头
    setCorsHeaders(req);
    
    // 根据文件扩展名设置Content-Type
    std::string content_type = "application/octet-stream";
    size_t dot_pos = path.rfind('.');
    if (dot_pos != std::string::npos) {
        std::string ext = path.substr(dot_pos);
        if (ext == ".txt") content_type = "text/plain";
        else if (ext == ".html" || ext == ".htm") content_type = "text/html";
        else if (ext == ".css") content_type = "text/css";
        else if (ext == ".js") content_type = "application/javascript";
        else if (ext == ".json") content_type = "application/json";
        else if (ext == ".png") content_type = "image/png";
        else if (ext == ".jpg" || ext == ".jpeg") content_type = "image/jpeg";
        else if (ext == ".gif") content_type = "image/gif";
        else if (ext == ".epub") content_type = "application/epub+zip";
        else if (ext == ".pdf") content_type = "application/pdf";
    }
    
    httpd_resp_set_type(req, content_type.c_str());
    
    // 设置Content-Disposition用于下载
    std::string filename = path.substr(path.rfind('/') + 1);
    std::string disposition = "attachment; filename=\"" + filename + "\"";
    httpd_resp_set_hdr(req, "Content-Disposition", disposition.c_str());
    
    // 分块发送文件
    char* buffer = new char[FILE_BUFFER_SIZE];
    size_t read_bytes;
    
    while ((read_bytes = fread(buffer, 1, FILE_BUFFER_SIZE, fp)) > 0) {
        if (httpd_resp_send_chunk(req, buffer, read_bytes) != ESP_OK) {
            mclog::tagError(TAG, "Failed to send file chunk");
            break;
        }
    }
    
    // 发送结束标记
    httpd_resp_send_chunk(req, nullptr, 0);
    
    delete[] buffer;
    fclose(fp);
    
    return ESP_OK;
}

// POST /api/file?path=/path/to/file
esp_err_t HttpFileServer::handlePostFile(httpd_req_t* req)
{
    std::string path = getQueryParam(req, "path");
    if (path.empty()) {
        sendErrorResponse(req, 400, "Path parameter required");
        return ESP_OK;
    }
    
    std::string full_path = SD_ROOT + path;
    mclog::tagInfo(TAG, "POST /api/file path={}, size={}", full_path, req->content_len);
    
    FILE* fp = fopen(full_path.c_str(), "wb");
    if (fp == nullptr) {
        mclog::tagError(TAG, "Failed to create file: {} (errno={})", full_path, errno);
        sendErrorResponse(req, 500, "Failed to create file");
        return ESP_OK;
    }
    
    // 接收并写入文件
    char* buffer = new char[FILE_BUFFER_SIZE];
    int remaining = req->content_len;
    int received;
    size_t total_written = 0;
    
    while (remaining > 0) {
        int to_read = std::min(remaining, (int)FILE_BUFFER_SIZE);
        received = httpd_req_recv(req, buffer, to_read);
        
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            mclog::tagError(TAG, "Failed to receive data");
            break;
        }
        
        size_t written = fwrite(buffer, 1, received, fp);
        if (written != (size_t)received) {
            mclog::tagError(TAG, "Failed to write data");
            break;
        }
        
        total_written += written;
        remaining -= received;
    }
    
    delete[] buffer;
    fclose(fp);
    
    if (remaining > 0) {
        // 删除不完整的文件
        remove(full_path.c_str());
        sendErrorResponse(req, 500, "File upload incomplete");
        return ESP_OK;
    }
    
    mclog::tagInfo(TAG, "File uploaded successfully: {} bytes", total_written);
    
    char json[128];
    snprintf(json, sizeof(json), "{\"success\":true,\"path\":\"%s\",\"size\":%zu}", path.c_str(), total_written);
    sendJsonResponse(req, json);
    
    return ESP_OK;
}

// DELETE /api/file?path=/path/to/file
esp_err_t HttpFileServer::handleDeleteFile(httpd_req_t* req)
{
    std::string path = getQueryParam(req, "path");
    if (path.empty()) {
        sendErrorResponse(req, 400, "Path parameter required");
        return ESP_OK;
    }
    
    std::string full_path = SD_ROOT + path;
    mclog::tagInfo(TAG, "DELETE /api/file path={}", full_path);
    
    struct stat st;
    if (stat(full_path.c_str(), &st) != 0) {
        sendErrorResponse(req, 404, "File not found");
        return ESP_OK;
    }
    
    int ret;
    if (S_ISDIR(st.st_mode)) {
        ret = rmdir(full_path.c_str());
    } else {
        ret = remove(full_path.c_str());
    }
    
    if (ret != 0) {
        mclog::tagError(TAG, "Failed to delete: {} (errno={})", full_path, errno);
        sendErrorResponse(req, 500, "Failed to delete");
        return ESP_OK;
    }
    
    mclog::tagInfo(TAG, "Deleted successfully: {}", full_path);
    
    char json[128];
    snprintf(json, sizeof(json), "{\"success\":true,\"path\":\"%s\"}", path.c_str());
    sendJsonResponse(req, json);
    
    return ESP_OK;
}

// POST /api/mkdir?path=/path/to/dir
esp_err_t HttpFileServer::handleMkdir(httpd_req_t* req)
{
    std::string path = getQueryParam(req, "path");
    if (path.empty()) {
        sendErrorResponse(req, 400, "Path parameter required");
        return ESP_OK;
    }
    
    std::string full_path = SD_ROOT + path;
    mclog::tagInfo(TAG, "POST /api/mkdir path={}", full_path);
    
    int ret = mkdir(full_path.c_str(), 0755);
    if (ret != 0 && errno != EEXIST) {
        mclog::tagError(TAG, "Failed to create directory: {} (errno={})", full_path, errno);
        sendErrorResponse(req, 500, "Failed to create directory");
        return ESP_OK;
    }
    
    mclog::tagInfo(TAG, "Directory created: {}", full_path);
    
    char json[128];
    snprintf(json, sizeof(json), "{\"success\":true,\"path\":\"%s\"}", path.c_str());
    sendJsonResponse(req, json);
    
    return ESP_OK;
}

// 递归删除目录
bool HttpFileServer::removeDirectoryRecursive(const std::string& path)
{
    DIR* dir = opendir(path.c_str());
    if (dir == nullptr) {
        return false;
    }
    
    struct dirent* entry;
    bool success = true;
    
    while ((entry = readdir(dir)) != nullptr) {
        // 跳过 . 和 ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        std::string item_path = path + "/" + entry->d_name;
        struct stat st;
        
        if (stat(item_path.c_str(), &st) != 0) {
            success = false;
            continue;
        }
        
        if (S_ISDIR(st.st_mode)) {
            // 递归删除子目录
            if (!removeDirectoryRecursive(item_path)) {
                success = false;
            }
        } else {
            // 删除文件
            if (remove(item_path.c_str()) != 0) {
                mclog::tagError(TAG, "Failed to delete file: {}", item_path);
                success = false;
            }
        }
    }
    
    closedir(dir);
    
    // 删除空目录本身
    if (rmdir(path.c_str()) != 0) {
        mclog::tagError(TAG, "Failed to delete directory: {}", path);
        success = false;
    }
    
    return success;
}

// DELETE /api/rmdir?path=/path/to/dir - 递归删除目录
esp_err_t HttpFileServer::handleRmdir(httpd_req_t* req)
{
    std::string path = getQueryParam(req, "path");
    if (path.empty()) {
        sendErrorResponse(req, 400, "Path parameter required");
        return ESP_OK;
    }
    
    // 安全检查：不允许删除根目录
    if (path == "/" || path == "/sdcard") {
        sendErrorResponse(req, 400, "Cannot delete root directory");
        return ESP_OK;
    }
    
    std::string full_path = SD_ROOT + path;
    mclog::tagInfo(TAG, "DELETE /api/rmdir path={}", full_path);
    
    struct stat st;
    if (stat(full_path.c_str(), &st) != 0) {
        sendErrorResponse(req, 404, "Directory not found");
        return ESP_OK;
    }
    
    if (!S_ISDIR(st.st_mode)) {
        sendErrorResponse(req, 400, "Path is not a directory");
        return ESP_OK;
    }
    
    if (!removeDirectoryRecursive(full_path)) {
        sendErrorResponse(req, 500, "Failed to delete directory completely");
        return ESP_OK;
    }
    
    mclog::tagInfo(TAG, "Directory deleted recursively: {}", full_path);
    
    char json[128];
    snprintf(json, sizeof(json), "{\"success\":true,\"path\":\"%s\"}", path.c_str());
    sendJsonResponse(req, json);
    
    return ESP_OK;
}

// 递归创建目录
bool HttpFileServer::createDirectoryRecursive(const std::string& path)
{
    std::string current;
    size_t pos = 0;
    
    while ((pos = path.find('/', pos + 1)) != std::string::npos) {
        current = path.substr(0, pos);
        if (current.length() > strlen(SD_ROOT)) {
            struct stat st;
            if (stat(current.c_str(), &st) != 0) {
                if (mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) {
                    return false;
                }
            }
        }
    }
    
    // 创建最终目录
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        if (mkdir(path.c_str(), 0755) != 0 && errno != EEXIST) {
            return false;
        }
    }
    
    return true;
}

// POST /api/upload-batch?dir=/target/dir - 批量上传文件
// Content-Type: multipart/form-data
// 每个文件的name字段为相对路径（可包含子目录）
esp_err_t HttpFileServer::handleUploadBatch(httpd_req_t* req)
{
    std::string target_dir = getQueryParam(req, "dir");
    if (target_dir.empty()) {
        target_dir = "/";
    }
    
    std::string base_path = SD_ROOT + target_dir;
    mclog::tagInfo(TAG, "POST /api/upload-batch dir={}, size={}", base_path, req->content_len);
    
    // 获取Content-Type头部以解析boundary
    char content_type[256] = {0};
    if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) != ESP_OK) {
        sendErrorResponse(req, 400, "Content-Type header required");
        return ESP_OK;
    }
    
    // 查找boundary
    char* boundary_start = strstr(content_type, "boundary=");
    if (boundary_start == nullptr) {
        sendErrorResponse(req, 400, "Boundary not found in Content-Type");
        return ESP_OK;
    }
    boundary_start += 9; // 跳过 "boundary="
    
    std::string boundary = "--";
    boundary += boundary_start;
    std::string boundary_end = boundary + "--";
    
    mclog::tagInfo(TAG, "Boundary: {}", boundary);
    
    // 分配缓冲区
    size_t buf_size = FILE_BUFFER_SIZE * 2;
    char* buffer = new char[buf_size];
    if (!buffer) {
        sendErrorResponse(req, 500, "Memory allocation failed");
        return ESP_OK;
    }
    
    int remaining = req->content_len;
    int total_received = 0;
    int file_count = 0;
    std::string json_result = "{\"success\":true,\"files\":[";
    
    // 状态机变量
    std::string accumulated_data;
    std::string current_filename;
    FILE* current_file = nullptr;
    bool in_file_content = false;
    
    while (remaining > 0) {
        int to_read = std::min(remaining, (int)buf_size);
        int received = httpd_req_recv(req, buffer, to_read);
        
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            break;
        }
        
        remaining -= received;
        total_received += received;
        
        // 将数据添加到累积缓冲区
        accumulated_data.append(buffer, received);
        
        // 处理累积的数据
        while (true) {
            if (!in_file_content) {
                // 查找boundary
                size_t boundary_pos = accumulated_data.find(boundary);
                if (boundary_pos == std::string::npos) {
                    // 保留可能不完整的boundary
                    if (accumulated_data.length() > boundary.length()) {
                        accumulated_data = accumulated_data.substr(accumulated_data.length() - boundary.length());
                    }
                    break;
                }
                
                // 检查是否是结束boundary
                if (accumulated_data.substr(boundary_pos, boundary_end.length()) == boundary_end) {
                    accumulated_data.clear();
                    break;
                }
                
                // 查找头部结束位置（\r\n\r\n）
                size_t header_end = accumulated_data.find("\r\n\r\n", boundary_pos);
                if (header_end == std::string::npos) {
                    break; // 等待更多数据
                }
                
                // 解析头部获取文件名
                std::string headers = accumulated_data.substr(boundary_pos, header_end - boundary_pos);
                
                // 查找filename
                size_t filename_pos = headers.find("filename=\"");
                if (filename_pos != std::string::npos) {
                    filename_pos += 10;
                    size_t filename_end = headers.find("\"", filename_pos);
                    if (filename_end != std::string::npos) {
                        current_filename = headers.substr(filename_pos, filename_end - filename_pos);
                        
                        // URL解码文件名
                        std::string decoded_filename;
                        for (size_t i = 0; i < current_filename.length(); i++) {
                            if (current_filename[i] == '%' && i + 2 < current_filename.length()) {
                                char hex[3] = {current_filename[i + 1], current_filename[i + 2], 0};
                                decoded_filename += (char)strtol(hex, nullptr, 16);
                                i += 2;
                            } else {
                                decoded_filename += current_filename[i];
                            }
                        }
                        current_filename = decoded_filename;
                        
                        // 构建完整文件路径
                        std::string file_path = base_path;
                        if (file_path.back() != '/') file_path += '/';
                        file_path += current_filename;
                        
                        // 确保父目录存在
                        size_t last_slash = file_path.rfind('/');
                        if (last_slash != std::string::npos) {
                            std::string parent_dir = file_path.substr(0, last_slash);
                            createDirectoryRecursive(parent_dir);
                        }
                        
                        mclog::tagInfo(TAG, "Receiving file: {}", file_path);
                        
                        current_file = fopen(file_path.c_str(), "wb");
                        if (current_file) {
                            in_file_content = true;
                        } else {
                            mclog::tagError(TAG, "Failed to create file: {}", file_path);
                        }
                    }
                }
                
                // 移除已处理的头部
                accumulated_data = accumulated_data.substr(header_end + 4);
            } else {
                // 在文件内容中，查找下一个boundary
                size_t next_boundary = accumulated_data.find(boundary);
                
                if (next_boundary != std::string::npos) {
                    // 找到boundary，写入之前的内容（去掉\r\n）
                    size_t content_end = next_boundary;
                    if (content_end >= 2 && accumulated_data[content_end - 2] == '\r' && accumulated_data[content_end - 1] == '\n') {
                        content_end -= 2;
                    }
                    
                    if (current_file && content_end > 0) {
                        fwrite(accumulated_data.data(), 1, content_end, current_file);
                    }
                    
                    if (current_file) {
                        fclose(current_file);
                        current_file = nullptr;
                        
                        if (file_count > 0) json_result += ",";
                        json_result += "\"" + current_filename + "\"";
                        file_count++;
                    }
                    
                    in_file_content = false;
                    accumulated_data = accumulated_data.substr(next_boundary);
                } else {
                    // 没找到boundary，写入数据（保留可能的部分boundary）
                    size_t safe_len = accumulated_data.length() > boundary.length() + 2 ?
                                      accumulated_data.length() - boundary.length() - 2 : 0;
                    
                    if (current_file && safe_len > 0) {
                        fwrite(accumulated_data.data(), 1, safe_len, current_file);
                        accumulated_data = accumulated_data.substr(safe_len);
                    }
                    break;
                }
            }
        }
    }
    
    // 关闭任何未关闭的文件
    if (current_file) {
        fclose(current_file);
        if (file_count > 0) json_result += ",";
        json_result += "\"" + current_filename + "\"";
        file_count++;
    }
    
    delete[] buffer;
    
    json_result += "],\"count\":" + std::to_string(file_count) + "}";
    
    mclog::tagInfo(TAG, "Batch upload complete: {} files", file_count);
    sendJsonResponse(req, json_result.c_str());
    
    return ESP_OK;
}
