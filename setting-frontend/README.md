# PaperS3 EPUB 处理器

基于 Vite + React 构建的前端应用，用于将 EPUB 电子书转换为适配 M5PaperS3 (960×540) 墨水屏的图片序列。

## 功能特性

- ✅ EPUB 文件上传与解析
- ✅ 强制统一排版（针对墨水屏优化）
- ✅ 自动分页计算
- ✅ Canvas 渲染页面为图片
- ✅ QOI / PNG 格式输出
- ✅ 打包下载为 ZIP 文件

## 技术栈

- **框架**: Vite + React + TypeScript
- **EPUB 解析**: epub.js
- **图片编码**: QOI（自实现）、PNG（Canvas API）
- **文件打包**: JSZip

## 快速开始

### 1. 安装依赖

```bash
npm install
```

### 2. 启动开发服务器

```bash
npm run dev
```

访问 http://localhost:3000

### 3. 构建生产版本

```bash
npm run build
```

## 项目结构

```
setting-frontend/
├── src/
│   ├── components/          # React 组件
│   │   ├── FileUpload.tsx   # 文件上传组件
│   │   └── ProgressDisplay.tsx  # 进度显示组件
│   ├── services/            # 业务逻辑
│   │   ├── epubProcessor.ts # EPUB 处理主流程
│   │   └── pageRenderer.ts  # 页面渲染服务
│   ├── utils/               # 工具函数
│   │   ├── imageEncoder.ts  # QOI/PNG 编码器
│   │   └── deviceClient.ts  # 设备 API 客户端
│   ├── types.ts             # TypeScript 类型定义
│   ├── App.tsx              # 主应用组件
│   └── main.tsx             # 入口文件
├── index.html
├── package.json
├── tsconfig.json
└── vite.config.ts
```

## 使用说明

1. **上传 EPUB 文件**
   - 点击上传区域或拖放 EPUB 文件
   - 支持标准 EPUB 2.0 / 3.0 格式

2. **处理过程**
   - 自动解析 EPUB 内容
   - 应用统一排版样式
   - 计算分页（960×540）
   - 渲染每一页为图片
   - 编码为 QOI 格式

3. **输出结果**
   - 自动下载 ZIP 文件
   - 包含 manifest.json 和所有页面图片

## 输出格式

### manifest.json

```json
{
  "title": "书名",
  "pageCount": 312,
  "width": 960,
  "height": 540,
  "format": "qoi"
}
```

### 页面图片

- 格式: QOI / PNG
- 分辨率: 960 × 540
- 命名: 0001.qoi, 0002.qoi, ...

## 排版参数

针对中文阅读优化的默认样式：

```css
body {
  font-family: "Source Han Serif", "Noto Serif SC", serif;
  font-size: 22px;
  line-height: 1.55;
  padding: 32px 28px;
}

p {
  text-indent: 2em;
  margin-bottom: 0.8em;
}
```

## 已知限制（MVP 阶段）

- ❌ 不保留 EPUB 原始样式
- ❌ 不支持复杂 PDF
- ❌ 页面渲染使用简化方法（需要集成 html2canvas）
- ❌ 分页算法为估算（需改进）

## TODO

- [ ] 集成 html2canvas 实现精确页面截图
- [ ] 优化分页算法（精确计算总页数）
- [ ] 添加 Web Worker 支持（后台处理）
- [ ] 实现与 ESP32 设备的直接通信
- [ ] 添加配置界面（字体、字号、边距等）

## 开发说明

### 添加新的图片格式

在 [imageEncoder.ts](src/utils/imageEncoder.ts) 中添加新的编码函数：

```typescript
export async function encodeToWebP(canvas: HTMLCanvasElement): Promise<Blob> {
  // 实现 WebP 编码
}
```

### 自定义排版样式

修改 [epubProcessor.ts](src/services/epubProcessor.ts) 中的 `rendition.themes.default()` 配置。

## 许可证

MIT

## 相关文档

- [项目设计文档](paper_s_3_前端项目设计文档（epub_→_序列图_mvp）.md)
- [QOI 图片格式规范](https://qoiformat.org/)
- [epub.js 文档](https://github.com/futurepress/epub.js)
