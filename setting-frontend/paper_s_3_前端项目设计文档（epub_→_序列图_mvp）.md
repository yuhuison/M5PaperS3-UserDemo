# PaperS3 前端项目设计文档（MVP）

> **目标一句话版**：
> 在用户的手机/电脑浏览器中，完成 **EPUB 上传 → 统一排版 → 分页 → 渲染为图片序列**，再交由设备侧存储与显示。

---

## 1. 项目目标与设计原则

### 1.1 MVP 核心目标

- 用户通过浏览器访问 PaperS3 暴露的本地页面
- 上传 **EPUB 文件**
- 前端完成：
  - EPUB 解析
  - 强制统一排版（为 4.7" 960×540 墨水屏优化）
  - 分页
  - 渲染为 **图片序列**（推荐 QOI / PNG）
- 将图片序列 + 元信息发送给设备

> **MVP 不做的事**：
> - 不做 EPUB 编辑器
> - 不保留 EPUB 原始样式
> - 不支持复杂 PDF
> - 不涉及云后端 / 账号系统

---

### 1.2 核心设计原则

1. **版式控制权在我们这**
   - EPUB 只作为“内容容器”，不是显示规范
2. **一次重排，设备侧零计算**
   - 所有复杂排版在前端完成
3. **页面即资产（Page-as-Asset）**
   - 每一页 = 一张可独立显示的图片
4. **设备侧 API 极简**
   - 只需要：接收文件、索引、显示

---

## 2. 前端整体架构（MVP）

```
┌──────────────┐
│   用户浏览器   │
│              │
│  上传 EPUB    │
│      ↓       │
│ EPUB 解析    │
│      ↓       │
│ 强制排版 CSS │
│      ↓       │
│ 分页计算     │
│      ↓       │
│ Canvas 渲染  │
│      ↓       │
│ 图片序列输出 │
│      ↓       │
│ HTTP 上传    │
└──────────────┘
            ↓
      ESP32-PaperS3
```

---

## 3. 推荐技术栈（前端）

### 3.1 核心技术选型（推荐）

| 模块 | 技术 | 选择原因 |
|---|---|---|
| 前端框架 | **Vite + React** | 快、生态成熟、canvas/worker 支持好 |
| EPUB 解析 | **epub.js** | 事实标准，支持 CFI / spine |
| 排版渲染 | DOM + CSS | 可控、可测、易调优 |
| 图片生成 | Canvas API | 与 epub.js 兼容性最好 |
| 图像格式 | **QOI / PNG** | QOI 解码快，PNG 兼容性好 |
| 任务调度 | Web Worker | 防止大书卡 UI |
| 文件打包 | JSZip | EPUB / CBZ / 输出打包 |

> ❗ MVP 阶段**不推荐**：
> - wasm 排版引擎
> - 自研 EPUB parser
> - headless browser

---

### 3.2 可选增强（非 MVP）

- OffscreenCanvas（Chrome / Android WebView）
- WASM 图像处理（如 dithering）
- OCR / LLM 翻译插件（后续）

---

## 4. EPUB → 页面序列：核心流程设计

### 4.1 EPUB 处理流程

1. 用户选择 EPUB 文件
2. 前端使用 epub.js 加载
3. **忽略原始 CSS**
4. 注入我们自己的排版规则
5. 以固定 viewport 渲染
6. 按“屏”为单位分页
7. 每页渲染成图片

---

### 4.2 固定页面规格（强约束）

| 参数 | 值 |
|---|---|
| 目标分辨率 | **960 × 540** |
| DPI 逻辑 | 1:1（非物理 DPI） |
| 方向 | 横屏 / 竖屏（择一） |
| 背景色 | #FFFFFF |
| 前景色 | #000000 |

---

### 4.3 推荐基础排版参数（中文友好）

```css
body {
  margin: 0;
  padding: 32px 28px;
  font-family: "Source Han Serif", "Noto Serif SC", serif;
  font-size: 22px;
  line-height: 1.55;
  color: #000;
  background: #fff;
}

p {
  margin: 0 0 0.8em 0;
  text-indent: 2em;
}

img {
  max-width: 100%;
  display: block;
  margin: 1em auto;
}
```

> MVP 阶段：**宁可简单、统一，也不要可配置**。

---

## 5. 分页策略设计（关键）

### 5.1 分页原则

- **一页 = 一个完整 viewport**
- 不跨页渲染（避免撕裂）
- 允许段落在页边截断（MVP 可接受）

### 5.2 实现思路

- 使用 epub.js rendition
- 固定 iframe/container 尺寸
- 每次 `rendition.display(cfi)`
- 监听 content 高度
- 逐屏偏移截图

---

## 6. 图片生成策略

### 6.1 Canvas 渲染方式

- DOM → Canvas
- 推荐方案：
  - epub.js + 内置 iframe
  - `drawImage(iframe, ...)`

### 6.2 输出格式建议

| 格式 | 用途 |
|---|---|
| PNG | 调试 / 兼容模式 |
| **QOI** | 设备侧正式使用 |

> QOI 优点：
> - 编码/解码极快
> - 实现简单（ESP32 可行）
> - 非有损

---

## 7. 输出数据结构（前端生成）

### 7.1 目录结构（逻辑）

```
book/
├── manifest.json
├── pages/
│   ├── 0001.qoi
│   ├── 0002.qoi
│   └── ...
```

### 7.2 manifest.json（示例）

```json
{
  "title": "书名",
  "pageCount": 312,
  "width": 960,
  "height": 540,
  "format": "qoi",
  "spine": []
}
```

---

## 8. MVP 范围总结

### ✅ 本阶段完成

- EPUB 上传
- 统一排版
- 分页
- 图片序列生成
- 发送给设备

### ❌ 明确不做

- EPUB 样式保留
- PDF 精排
- 搜索 / 标注
- 云同步

---

## 9. 下一阶段自然演进方向（仅占位）

- 放大镜（基于母图）
- 漫画气泡 OCR + 翻译叠加
- 多语言排版 preset
- 局部重渲染（节省存储）

---

> **结语**：
> 这个前端项目的本质不是“阅读器 UI”，而是一个 **排版编译器**。
> EPUB 只是输入语言，图片页才是你的 IR（中间表示）。
