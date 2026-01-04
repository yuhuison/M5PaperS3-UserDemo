# M5PaperS3 HTTP文件服务器API文档

## 概述

M5PaperS3设备在连接WiFi后可以启动HTTP文件服务器，提供SD卡文件管理功能。所有API都支持CORS跨域访问，可直接从Web应用调用。

**基础URL**: `http://<设备IP地址>`  
**端口**: 80  
**支持格式**: JSON

## 获取设备IP地址

1. 在设备上连接WiFi成功后，屏幕会显示IP地址
2. 调用 `/api/info` 端点也会返回IP地址

---

## API端点

### 1. 获取设备信息

获取设备基本信息，包括IP地址、WiFi状态和存储空间。

**端点**: `GET /api/info`

**请求示例**:
```bash
curl http://192.168.1.100/api/info
```

**响应示例**:
```json
{
  "device": "M5PaperS3",
  "ip": "192.168.1.100",
  "wifi": {
    "ssid": "MyWiFi",
    "rssi": -45
  },
  "storage": {
    "total": 32212254720,
    "free": 30123456789,
    "used": 2088797931
  }
}
```

**响应字段说明**:
| 字段 | 类型 | 说明 |
|------|------|------|
| device | string | 设备型号 |
| ip | string | 设备IP地址 |
| wifi.ssid | string | 连接的WiFi名称 |
| wifi.rssi | number | WiFi信号强度（dBm） |
| storage.total | number | SD卡总容量（字节） |
| storage.free | number | SD卡剩余空间（字节） |
| storage.used | number | SD卡已用空间（字节） |

---

### 2. 列出目录内容

列出指定目录下的所有文件和子目录。

**端点**: `GET /api/list?path=<目录路径>`

**查询参数**:
| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| path | string | 否 | 目录路径，默认为 `/`（根目录） |

**请求示例**:
```bash
# 列出根目录
curl http://192.168.1.100/api/list?path=/

# 列出books目录
curl http://192.168.1.100/api/list?path=/books
```

**响应示例**:
```json
{
  "path": "/books",
  "items": [
    {
      "name": "novel.epub",
      "type": "file",
      "size": 2457600
    },
    {
      "name": "documents",
      "type": "directory"
    },
    {
      "name": "readme.txt",
      "type": "file",
      "size": 1024
    }
  ]
}
```

**响应字段说明**:
| 字段 | 类型 | 说明 |
|------|------|------|
| path | string | 当前目录路径 |
| items | array | 文件和目录列表 |
| items[].name | string | 文件/目录名称 |
| items[].type | string | 类型：`file` 或 `directory` |
| items[].size | number | 文件大小（字节），仅文件有此字段 |

---

### 3. 下载文件

下载指定路径的文件。

**端点**: `GET /api/file?path=<文件路径>`

**查询参数**:
| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| path | string | 是 | 文件路径（相对于SD卡根目录） |

**请求示例**:
```bash
# 下载文件
curl http://192.168.1.100/api/file?path=/books/novel.epub -o novel.epub

# 使用wget
wget http://192.168.1.100/api/file?path=/books/novel.epub
```

**JavaScript示例**:
```javascript
// 下载文件
fetch('http://192.168.1.100/api/file?path=/books/novel.epub')
  .then(response => response.blob())
  .then(blob => {
    const url = window.URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'novel.epub';
    a.click();
  });
```

**响应**:
- **成功**: 返回文件内容（二进制流）
- **Content-Type**: 根据文件扩展名自动设置
- **Content-Disposition**: `attachment; filename="文件名"`

**支持的Content-Type**:
| 扩展名 | Content-Type |
|--------|--------------|
| .txt | text/plain |
| .html, .htm | text/html |
| .css | text/css |
| .js | application/javascript |
| .json | application/json |
| .png | image/png |
| .jpg, .jpeg | image/jpeg |
| .gif | image/gif |
| .epub | application/epub+zip |
| .pdf | application/pdf |
| 其他 | application/octet-stream |

---

### 4. 上传文件

上传文件到指定路径。如果文件已存在，将被覆盖。**会自动创建父目录**（如果不存在）。

**端点**: `POST /api/file?path=<文件路径>`

**查询参数**:
| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| path | string | 是 | 文件保存路径（相对于SD卡根目录） |

**请求头**:
- `Content-Type`: 任意（文件的MIME类型）

**请求体**: 文件的二进制内容

**请求示例**:
```bash
# 使用curl上传
curl -X POST \
  -H "Content-Type: application/epub+zip" \
  --data-binary @novel.epub \
  http://192.168.1.100/api/file?path=/books/novel.epub

# 上传文本文件
curl -X POST \
  -H "Content-Type: text/plain" \
  --data-binary @readme.txt \
  http://192.168.1.100/api/file?path=/readme.txt
```

**JavaScript示例**:
```javascript
// 上传文件（从文件输入）
const fileInput = document.querySelector('input[type="file"]');
const file = fileInput.files[0];

fetch(`http://192.168.1.100/api/file?path=/books/${file.name}`, {
  method: 'POST',
  headers: {
    'Content-Type': file.type
  },
  body: file
})
.then(response => response.json())
.then(data => console.log('上传成功:', data));

// 上传Blob/ArrayBuffer
const blob = new Blob(['Hello World'], { type: 'text/plain' });
fetch('http://192.168.1.100/api/file?path=/test.txt', {
  method: 'POST',
  body: blob
})
.then(response => response.json())
.then(data => console.log('上传成功:', data));
```

**响应示例**:
```json
{
  "success": true,
  "path": "/books/novel.epub",
  "size": 2457600
}
```

**响应字段说明**:
| 字段 | 类型 | 说明 |
|------|------|------|
| success | boolean | 是否成功 |
| path | string | 文件路径 |
| size | number | 上传的文件大小（字节） |

---

### 5. 删除文件或目录

删除指定的文件或空目录。

**端点**: `DELETE /api/file?path=<路径>`

**查询参数**:
| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| path | string | 是 | 要删除的文件或目录路径 |

**注意事项**:
- 只能删除空目录
- 删除操作不可恢复

**请求示例**:
```bash
# 删除文件
curl -X DELETE http://192.168.1.100/api/file?path=/test.txt

# 删除空目录
curl -X DELETE http://192.168.1.100/api/file?path=/empty_folder
```

**JavaScript示例**:
```javascript
fetch('http://192.168.1.100/api/file?path=/test.txt', {
  method: 'DELETE'
})
.then(response => response.json())
.then(data => console.log('删除成功:', data));
```

**响应示例**:
```json
{
  "success": true,
  "path": "/test.txt"
}
```

---

### 6. 创建目录

创建新目录。**支持递归创建**（类似 `mkdir -p`），会自动创建所有不存在的父目录。如果目录已存在，不会报错。

**端点**: `POST /api/mkdir?path=<目录路径>`

**查询参数**:
| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| path | string | 是 | 要创建的目录路径 |

**请求示例**:
```bash
# 创建单级目录
curl -X POST http://192.168.1.100/api/mkdir?path=/books

# 创建多级目录（自动创建所有父目录）
curl -X POST http://192.168.1.100/api/mkdir?path=/books/novels/scifi
```

**JavaScript示例**:
```javascript
fetch('http://192.168.1.100/api/mkdir?path=/books', {
  method: 'POST'
})
.then(response => response.json())
.then(data => console.log('创建成功:', data));
```

**响应示例**:
```json
{
  "success": true,
  "path": "/books"
}
```

---

### 7. 递归删除目录

删除目录及其所有内容（文件和子目录）。

**端点**: `DELETE /api/rmdir?path=<目录路径>`

**查询参数**:
| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| path | string | 是 | 要删除的目录路径 |

**警告**: 此操作不可恢复！会删除目录下所有文件和子目录。

**请求示例**:
```bash
curl -X DELETE "http://192.168.1.100/api/rmdir?path=/books/old_book"
```

**响应示例**:
```json
{
  "success": true,
  "path": "/books/old_book"
}
```

---

### 8. 批量上传文件

使用 multipart/form-data 格式一次上传多个文件。

**端点**: `POST /api/upload-batch?dir=<目标目录>`

**查询参数**:
| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| dir | string | 否 | 目标目录，默认为根目录 |

**请求示例**:
```bash
curl -X POST "http://192.168.1.100/api/upload-batch?dir=/books/new_book" \
  -F "file=@cover.png;filename=cover.png" \
  -F "file=@page1.png;filename=sections/001/001.png"
```

**响应示例**:
```json
{
  "success": true,
  "files": ["cover.png", "sections/001/001.png"],
  "count": 2
}
```

---

## 错误响应

所有API在发生错误时返回统一的错误格式：

**错误响应格式**:
```json
{
  "error": true,
  "code": 404,
  "message": "File not found"
}
```

**错误字段说明**:
| 字段 | 类型 | 说明 |
|------|------|------|
| error | boolean | 固定为 `true` |
| code | number | HTTP状态码 |
| message | string | 错误描述 |

**常见错误码**:
| 状态码 | 说明 | 可能原因 |
|--------|------|----------|
| 400 | Bad Request | 缺少必填参数或参数格式错误 |
| 404 | Not Found | 文件或目录不存在 |
| 500 | Internal Server Error | 服务器内部错误（如SD卡未挂载、权限问题） |

**错误示例**:
```json
// 文件不存在
{
  "error": true,
  "code": 404,
  "message": "File not found"
}

// 缺少path参数
{
  "error": true,
  "code": 400,
  "message": "Path parameter required"
}

// SD卡操作失败
{
  "error": true,
  "code": 500,
  "message": "Failed to create file"
}
```

---

## 使用示例

### Python客户端

```python
import requests
import json

# 设备IP
BASE_URL = 'http://192.168.1.100'

# 1. 获取设备信息
def get_device_info():
    response = requests.get(f'{BASE_URL}/api/info')
    return response.json()

# 2. 列出目录
def list_directory(path='/'):
    response = requests.get(f'{BASE_URL}/api/list', params={'path': path})
    return response.json()

# 3. 上传文件
def upload_file(local_path, remote_path):
    with open(local_path, 'rb') as f:
        response = requests.post(
            f'{BASE_URL}/api/file',
            params={'path': remote_path},
            data=f
        )
    return response.json()

# 4. 下载文件
def download_file(remote_path, local_path):
    response = requests.get(f'{BASE_URL}/api/file', params={'path': remote_path})
    with open(local_path, 'wb') as f:
        f.write(response.content)

# 5. 删除文件
def delete_file(path):
    response = requests.delete(f'{BASE_URL}/api/file', params={'path': path})
    return response.json()

# 6. 创建目录
def create_directory(path):
    response = requests.post(f'{BASE_URL}/api/mkdir', params={'path': path})
    return response.json()

# 使用示例
if __name__ == '__main__':
    # 获取设备信息
    info = get_device_info()
    print(f"设备IP: {info['ip']}")
    print(f"剩余空间: {info['storage']['free'] / 1024 / 1024 / 1024:.2f} GB")
    
    # 列出根目录
    files = list_directory('/')
    for item in files['items']:
        print(f"{item['type']}: {item['name']}")
    
    # 上传电子书
    upload_file('mybook.epub', '/books/mybook.epub')
    
    # 下载文件
    download_file('/books/mybook.epub', 'downloaded.epub')
```

### JavaScript/TypeScript客户端

```typescript
class M5PaperS3Client {
  private baseUrl: string;

  constructor(ip: string, port: number = 80) {
    this.baseUrl = `http://${ip}:${port}`;
  }

  // 获取设备信息
  async getDeviceInfo() {
    const response = await fetch(`${this.baseUrl}/api/info`);
    return response.json();
  }

  // 列出目录
  async listDirectory(path: string = '/') {
    const response = await fetch(
      `${this.baseUrl}/api/list?path=${encodeURIComponent(path)}`
    );
    return response.json();
  }

  // 上传文件
  async uploadFile(file: File | Blob, remotePath: string) {
    const response = await fetch(
      `${this.baseUrl}/api/file?path=${encodeURIComponent(remotePath)}`,
      {
        method: 'POST',
        body: file
      }
    );
    return response.json();
  }

  // 下载文件
  async downloadFile(remotePath: string): Promise<Blob> {
    const response = await fetch(
      `${this.baseUrl}/api/file?path=${encodeURIComponent(remotePath)}`
    );
    return response.blob();
  }

  // 删除文件
  async deleteFile(path: string) {
    const response = await fetch(
      `${this.baseUrl}/api/file?path=${encodeURIComponent(path)}`,
      {
        method: 'DELETE'
      }
    );
    return response.json();
  }

  // 创建目录
  async createDirectory(path: string) {
    const response = await fetch(
      `${this.baseUrl}/api/mkdir?path=${encodeURIComponent(path)}`,
      {
        method: 'POST'
      }
    );
    return response.json();
  }

  // 批量上传文件
  async uploadFiles(files: File[], basePath: string = '/') {
    const results = [];
    for (const file of files) {
      const remotePath = `${basePath}/${file.name}`;
      const result = await this.uploadFile(file, remotePath);
      results.push(result);
    }
    return results;
  }

  // 递归列出所有文件
  async listAllFiles(path: string = '/'): Promise<any[]> {
    const result = await this.listDirectory(path);
    const files = [];
    
    for (const item of result.items) {
      const fullPath = `${path}/${item.name}`;
      if (item.type === 'file') {
        files.push({ ...item, path: fullPath });
      } else if (item.type === 'directory') {
        const subFiles = await this.listAllFiles(fullPath);
        files.push(...subFiles);
      }
    }
    
    return files;
  }
}

// 使用示例
const client = new M5PaperS3Client('192.168.1.100');

// 获取设备信息
const info = await client.getDeviceInfo();
console.log(`设备IP: ${info.ip}`);

// 上传文件
const fileInput = document.querySelector<HTMLInputElement>('input[type="file"]');
if (fileInput?.files?.[0]) {
  const result = await client.uploadFile(
    fileInput.files[0],
    `/books/${fileInput.files[0].name}`
  );
  console.log('上传成功:', result);
}

// 列出所有电子书
const books = await client.listDirectory('/books');
console.log('电子书列表:', books.items);
```

---

## 最佳实践

### 1. 文件上传方式选择

根据场景选择合适的上传方式：

| 场景 | 推荐方式 | 说明 |
|------|---------|------|
| 单个小文件 (<1MB) | `/api/file` | 简单直接 |
| 单个大文件 (>10MB) | `/api/file` | 支持进度显示 |
| **多个文件** | **`/api/upload-batch`** | **一次请求上传多文件** |
| 需要断点续传 | `/api/file` 逐个上传 | 失败后重传单个文件 |

### 2. 路径处理

- 路径必须以 `/` 开头（相对于SD卡根目录）
- URL编码特殊字符：`encodeURIComponent(path)`
- 文件名建议使用ASCII字符或UTF-8编码
- 避免使用Windows保留字符：`< > : " | ? *`

### 3. 大文件上传

对于大文件（>10MB），建议：
- 显示上传进度（使用XMLHttpRequest）
- 添加超时处理（建议timeout: 300000，即5分钟）
- 检查SD卡剩余空间（调用 `/api/info`）

```javascript
// 大文件上传示例（带进度和超时）
async function uploadWithProgress(file, remotePath, onProgress) {
  const xhr = new XMLHttpRequest();
  
  return new Promise((resolve, reject) => {
    xhr.upload.addEventListener('progress', (e) => {
      if (e.lengthComputable) {
        const percent = (e.loaded / e.total) * 100;
        onProgress(percent);
      }
    });
    
    xhr.addEventListener('load', () => {
      if (xhr.status === 200) {
        resolve(JSON.parse(xhr.responseText));
      } else {
        reject(new Error(`Upload failed: ${xhr.status}`));
      }
    });
    
    xhr.open('POST', `http://192.168.1.100/api/file?path=${encodeURIComponent(remotePath)}`);
    xhr.send(file);
  });
}

// 使用
await uploadWithProgress(file, '/books/large.pdf', (percent) => {
  console.log(`上传进度: ${percent.toFixed(1)}%`);
});
```

### 4. 错误处理

统一的错误处理封装：

```javascript
async function safeRequest(url, options) {
  try {
    const response = await fetch(url, options);
    
    // 检查HTTP状态
    if (!response.ok) {
      throw new Error(`HTTP Error ${response.status}: ${response.statusText}`);
    }
    
    const data = await response.json();
    
    // 检查API错误
    if (data.error) {
      throw new Error(`API Error ${data.code}: ${data.message}`);
    }
    
    return data;
  } catch (error) {
    if (error instanceof TypeError) {
      throw new Error('网络错误：无法连接到设备，请检查WiFi连接和IP地址');
    }
    throw error;
  }
}

// 使用示例
try {
  const result = await safeRequest(
    'http://192.168.1.100/api/file?path=/books/test.png',
    { method: 'POST', body: imageBlob }
  );
  console.log('上传成功:', result);
} catch (error) {
  console.error('上传失败:', error.message);
}
```

### 5. 批量操作

使用 `/api/upload-batch` 批量上传文件：

```javascript
// 批量上传文件（使用 multipart/form-data）
async function uploadBatch(files, targetDir) {
  const formData = new FormData();
  
  for (const file of files) {
    // 可以指定相对路径作为 filename
    formData.append('file', file.blob, file.path || file.name);
  }
  
  const response = await fetch(
    `http://192.168.1.100/api/upload-batch?dir=${encodeURIComponent(targetDir)}`,
    {
      method: 'POST',
      body: formData
    }
  );
  
  return response.json();
}

// 使用示例
const files = [
  { blob: coverBlob, path: 'cover.png' },
  { blob: page1Blob, path: 'sections/001/001.png' },
  { blob: page2Blob, path: 'sections/001/002.png' },
];

const result = await uploadBatch(files, '/books/my_book');
console.log(`成功上传 ${result.count} 个文件`);
```

---

## 安全注意事项

1. **局域网访问**: HTTP服务器仅在局域网内可访问，不暴露到公网
2. **无认证**: API不需要认证，任何可访问设备的客户端都可以操作文件
3. **文件覆盖**: 上传文件时会覆盖同名文件，无二次确认
4. **路径限制**: 所有操作限制在SD卡目录内（`/sdcard/`）
5. **并发限制**: 建议避免并发大量请求，可能导致设备响应缓慢

---

## 故障排除

### 问题1: 无法连接到API

**症状**: `fetch` 请求超时或失败

**解决方案**:
1. 确认设备和客户端在同一WiFi网络
2. 检查设备屏幕显示的IP地址是否正确
3. 尝试ping设备IP：`ping 192.168.1.100`
4. 确认HTTP服务器已启动（设备屏幕显示"HTTP服务器运行中"）

### 问题2: 文件上传失败

**症状**: 上传返回500错误

**可能原因**:
1. SD卡空间不足
2. SD卡未正确挂载
3. 文件路径包含非法字符
4. 目录不存在（需先创建目录）

**解决方案**:
- 检查剩余空间：`GET /api/info`
- 确保目标目录存在，或先创建
- 使用ASCII文件名

### 问题3: CORS错误

**症状**: 浏览器控制台显示CORS错误

**解决方案**:
- API已经配置CORS支持，允许所有来源
- 如果仍有问题，检查浏览器是否阻止混合内容（HTTPS页面访问HTTP API）
- 建议使用HTTP托管的前端页面，或使用浏览器扩展禁用CORS检查（仅开发环境）

---

## 更新日志

### v1.0.0 (2026-01-03)
- 初始版本
- 支持基本的文件上传、下载、删除
- 支持目录列表和创建
- 支持批量上传 (`/api/upload-batch`)
- 提供设备信息查询
- 支持CORS跨域访问

---

## 许可

MIT License - Copyright (c) 2025 M5Stack Technology CO LTD
