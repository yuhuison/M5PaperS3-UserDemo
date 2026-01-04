# M5PaperS3 设置前端项目总结

## 项目概述

这是 M5PaperS3 电子墨水屏设备的配套 Web 前端应用，主要功能是：
1. **EPUB 电子书转换** - 将 EPUB 文件转换为适配 M5PaperS3 的图片格式
2. **设备管理** - 通过 HTTP API 与设备通信，上传/管理电子书
3. **WiFi 配置** - 配置设备的 WiFi 连接

---

## 技术栈

| 类别 | 技术 |
|------|------|
| **框架** | React 18 + TypeScript |
| **构建工具** | Vite 5 |
| **EPUB 解析** | epubjs |
| **图片渲染** | Canvas 2D API（自研）/ html2canvas（备用） |
| **图片导出** | html-to-image |
| **ZIP 打包** | JSZip |

---

## 目录结构

```
setting-frontend/
├── src/
│   ├── api/
│   │   └── M5PaperS3Client.ts    # 设备 HTTP API 客户端
│   │
│   ├── components/
│   │   ├── EpubToImages.tsx      # 主要组件：EPUB 转图片
│   │   ├── CanvasRenderer.ts     # ⭐ 纯 Canvas 文本渲染器
│   │   ├── BookShelf.tsx         # 书架组件
│   │   ├── ConfigRenderer.tsx    # 配置渲染器
│   │   ├── PagePreview.tsx       # 页面预览组件
│   │   ├── FileUpload.tsx        # 文件上传组件
│   │   ├── ProgressDisplay.tsx   # 进度显示组件
│   │   ├── EpubDebugViewer.tsx   # EPUB 调试查看器
│   │   └── processors.ts         # 图片处理器
│   │
│   ├── services/
│   │   ├── backendApi.ts         # 后端 API 服务
│   │   ├── epubProcessor.ts      # EPUB 处理服务
│   │   └── pageRenderer.ts       # 页面渲染服务
│   │
│   ├── utils/
│   │   ├── deviceClient.ts       # 设备客户端工具
│   │   └── imageEncoder.ts       # 图片编码工具
│   │
│   ├── types.ts                  # 类型定义
│   ├── App.tsx                   # 主应用组件
│   └── main.tsx                  # 入口文件
│
├── package.json
├── vite.config.ts
└── tsconfig.json
```

---

## 核心功能模块

### 1. EPUB 转图片 (`EpubToImages.tsx`)

**功能**：将 EPUB 电子书转换为 M5PaperS3 可显示的图片序列

**页面规格**：
- 宽度：540px
- 高度：900px（每页）
- 重叠：100px（相邻页之间，用于翻页过渡）
- 步进：800px（PAGE_HEIGHT - OVERLAP_HEIGHT）
- 灰度：16级（适配电子墨水屏）

**两种渲染模式**：
| 模式 | 实现 | 优点 | 缺点 |
|------|------|------|------|
| **Canvas 模式** ⭐推荐 | `CanvasRenderer.ts` | 快速、无内存问题 | 只支持基本排版 |
| **html2canvas 模式** | html2canvas 库 | 支持复杂 CSS | 长章节会 OOM |

**输出文件结构**：
```
{bookId}/
├── metadata.json          # 书籍元数据
├── reading_status.json    # 阅读进度
├── cover.png              # 封面图片
└── sections/
    ├── 001/               # 第1章
    │   ├── 001.png
    │   ├── 002.png
    │   └── ...
    ├── 002/               # 第2章
    │   └── ...
    └── ...
```

---

### 2. Canvas 渲染器 (`CanvasRenderer.ts`)

**为什么需要它**：
- html2canvas 在处理长章节（60,000+ 像素高度）时会导致内存溢出
- 纯 Canvas 2D API 直接绘制文本，无 DOM 克隆开销

**核心函数**：
```typescript
// 解析 HTML 为简单内容块
parseHtmlToBlocks(html: string): ContentBlock[]

// 计算文本换行
wrapText(ctx: CanvasRenderingContext2D, text: string, maxWidth: number): string[]

// 渲染为多页图片
renderToPages(html: string, config: RenderConfig): Promise<string[]>

// 应用灰度量化
applyGrayscale(ctx, width, height, levels)
```

**性能对比**：
| 指标 | html2canvas | Canvas 渲染器 |
|------|-------------|--------------|
| 渲染速度 | 30-60秒 | 2-5秒 |
| 内存占用 | 高（会 OOM） | 低（流式处理） |
| 长章节支持 | ❌ | ✅ |

---

### 3. 设备 API 客户端 (`M5PaperS3Client.ts`)

**连接方式**：HTTP，设备作为服务器

**主要接口**：

```typescript
class M5PaperS3Client {
  // 设备信息
  getDeviceInfo(): Promise<DeviceInfo>
  testConnection(): Promise<boolean>
  
  // 文件操作
  listDirectory(path): Promise<DirectoryListing>
  uploadFile(blob, path): Promise<void>
  downloadFile(path): Promise<Blob>
  deleteFile(path): Promise<void>
  createDirectory(path): Promise<void>
  
  // ⭐ ZIP上传（推荐）
  uploadZip(zipBlob, targetDir, onProgress): Promise<{success, extracted, errors}>
  
  // 书籍管理
  getBookList(): Promise<BookMetadata[]>
  uploadBook(bookId, metadata, cover, sections, onProgress): Promise<void>
  deleteBook(bookId): Promise<void>
  getBookDetails(bookId): Promise<{metadata, coverUrl}>
}
```

**ZIP 上传 API**（⭐推荐）：
```
POST /api/upload-zip?dir=/books/{bookId}
Content-Type: application/zip

将所有文件打包为ZIP上传，设备端流式解压
- 使用STORE无压缩格式，速度5-8MB/s（⚡最快）
- 比批量上传快 10倍+
- 更稳定，避免多文件上传中断
- 内存安全，设备端流式解压（256KB缓冲）
```

**递归删除 API**：
```
DELETE /api/rmdir?path=/books/{bookId}
```

---

### 4. 数据结构

**BookMetadata**（书籍元数据）：
```typescript
interface BookMetadata {
  id: string;           // 唯一标识（目录名）
  title: string;        // 书名
  author?: string;      // 作者
  addedAt: string;      // 添加时间 ISO8601
  sections: SectionInfo[];
}

interface SectionInfo {
  index: number;        // 章节索引（从1开始）
  title: string;        // 章节标题
  pageCount: number;    // 该章节的页数
}
```

**ReadingStatus**（阅读进度）：
```typescript
interface ReadingStatus {
  currentSection: number;   // 当前章节
  currentPage: number;      // 当前页码
  lastReadTime: string;     // 最后阅读时间
}
```

---

## 开发命令

```bash
# 安装依赖
npm install

# 开发模式（热更新）
npm run dev

# 构建生产版本
npm run build

# 预览构建结果
npm run preview
```

---

## 设备 HTTP API 文档

参见 `docs/HTTP_API_DOCUMENTATION.md`

**基础端点**：
| 端点 | 方法 | 说明 |
|------|------|------|
| `/api/info` | GET | 获取设备信息 |
| `/api/list?path=` | GET | 列出目录 |
| `/api/file?path=` | GET | 下载文件 |
| `/api/file?path=` | POST | 上传文件 |
| `/api/file?path=` | DELETE | 删除文件 |
| `/api/mkdir?path=` | POST | 创建目录 |
| `/api/upload-batch?dir=` | POST | 批量上传 |
| `/api/rmdir?path=` | DELETE | 递归删除目录 |

---

## 已知限制

1. **Canvas 渲染器限制**：
   - 不支持复杂 CSS 布局（表格、多栏等）
   - 不支持自定义字体（使用系统字体）
   - 图片需要是 data URL 或可跨域访问

2. **EPUB 兼容性**：
   - 某些 DRM 保护的 EPUB 无法打开
   - 复杂排版的 EPUB 可能渲染效果不佳

---

## 性能优化

✅ **v1.2 - STORE无压缩优化**（当前版本）：
- 使用 STORE 无压缩格式替代 DEFLATE 压缩
- 上传速度从 500KB/s 提升到 5-8MB/s（**提升10倍+**）
- PNG图片等已压缩文件无需再次压缩
- 打包速度更快，无压缩开销

✅ **v1.1 - ZIP 上传优化**：
- 使用 `/api/upload-zip` 替代批量上传
- 上传速度提升 3-5 倍
- 更稳定可靠，无内存溢出问题

---

## 待办/可改进

- [ ] 支持更多 EPUB 元素（列表、表格）
- [ ] 支持自定义字体
- [ ] 优化图片压缩（进一步减小文件大小）
- [ ] 添加离线缓存支持

---

## 相关文件

- `docs/HTTP_API_DOCUMENTATION.md` - 设备 API 完整文档
- `paper_s_3_前端项目设计文档（epub_→_序列图_mvp）.md` - 原始设计文档
