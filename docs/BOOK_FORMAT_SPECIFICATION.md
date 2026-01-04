# 书籍文件格式规范

## 设备硬件限制

- **屏幕尺寸**: 540 × 960 像素（竖屏模式）
- **显示类型**: 电子墨水屏（E-Paper），16 级灰度
- **内存限制**: ESP32-S3，有限的 RAM
- **看门狗超时**: 单次渲染操作不能超过 5 秒

## 目录结构

```
/sdcard/books/
└── {book_id}/                          # 如: book_1704067200_abc123
    ├── metadata.json                   # 书籍元信息
    ├── reading_status.json             # 阅读进度（设备自动维护）
    ├── cover.png                       # 封面图片
    └── sections/                       # 章节目录（对应 EPUB 章节）
        ├── 000/                        # 第0章（章节索引从0开始）
        │   ├── 001.png                 # 第0章第1页（页码从1开始）
        │   ├── 002.png                 # 第0章第2页
        │   ├── links.json              # 本章节所有页面的链接信息（可选）
        │   └── ...
        ├── 001/                        # 第1章
        │   ├── 001.png
        │   ├── links.json
        │   └── ...
        └── ...
```

## 文件格式说明

### 1. metadata.json

```json
{
    "id": "book_1704067200_abc123",
    "title": "书名",
    "author": "作者",
    "addedAt": "2026-01-03T12:00:00Z",
    "sections": [
        {
            "index": 0,
            "title": "第一章 开篇",
            "pageCount": 15
        },
        {
            "index": 1,
            "title": "第二章 发展",
            "pageCount": 23
        }
    ],
    "anchorMap": {
        "chapter1": { "section": 0, "page": 1 },
        "section2-1": { "section": 1, "page": 1 },
        "important-note": { "section": 1, "page": 5 }
    }
}
```

**新增字段说明**：
- `anchorMap`: 锚点映射表（可选），将 HTML 锚点 ID 映射到具体的章节和页面
  - 键：锚点 ID（从 EPUB 中的 `id` 属性或 `<a name="">` 提取）
  - 值：`{ section: 章节索引（从0开始）, page: 页码（从1开始） }`

### 2. reading_status.json（设备自动创建和维护）

上传工具会创建初始状态，设备端负责维护更新：

```json
{
    "currentSection": 0,
    "currentPage": 1,
    "lastReadTime": "2026-01-03T14:30:00Z"
}
```

**注意**：
- `currentSection` 从 **0** 开始（对应目录 000/, 001/, ...）
- `currentPage` 从 **1** 开始（对应文件 001.png, 002.png, ...）
- 设备端在用户翻页时更新此文件
- `lastReadTime` 使用 ISO8601 格式

### 3. cover.png - 封面图片

- **格式**: PNG
- **尺寸**: 540 × 540 像素（正方形，用于列表显示）
- **颜色**: 灰度 8-bit（推荐）或 RGB
- **大小**: 建议 < 50KB

### 4. 页面文件 (sections/{section}/{page}.png)

- **格式**: PNG
- **尺寸**: 540 × 900 像素（**注意：不是 960！**）
- **颜色**: **必须**使用灰度 8-bit（无 Alpha 通道）
- **压缩**: 最高压缩级别
- **大小**: **必须** < 80KB（否则可能触发看门狗超时）

### 5. 链接信息文件 (sections/{section}/links.json)（可选）

如果章节中包含链接（`<a>` 标签），则生成此文件，包含所有页面的可点击链接区域信息。

**注意：即使某页没有链接，也会包含该页的元数据（用于标记是否有图片）。**

```json
{
    "pages": [
        {
            "page": 1,
            "hasImage": true,
            "links": [
                {
                    "text": "跳转到第三章",
                    "rect": {
                        "x": 100,
                        "y": 200,
                        "width": 150,
                        "height": 30
                    },
                    "href": "#chapter3",
                    "type": "internal",
                    "target": {
                        "section": 2,
                        "page": 1
                    }
                },
                {
                    "text": "外部链接",
                    "rect": {
                        "x": 50,
                        "y": 450,
                        "width": 200,
                        "height": 28
                    },
                    "href": "https://example.com",
                    "type": "external"
                }
            ]
        },
        {
            "page": 2,
            "hasImage": false,
            "links": []
        }
    ]
}
```

**字段说明**：

- `pages`: 页面数组，按页码顺序排列
  - `page`: 页码（从 1 开始）
  - `hasImage`: 该页是否包含图片（布尔值）
    - `true`: 页面包含图片，设备端应使用较慢的刷新模式以保证图片质量
    - `false`: 纯文本页面，设备端可使用快速刷新模式
  - `links`: 该页面的链接数组

- 链接对象字段：
  - `text`: 链接文本内容（最多 100 字符，超出则截断）
  - `rect`: 碰撞检测区域（相对于页面左上角的坐标）
    - `x`: 左上角 X 坐标（像素）
    - `y`: 左上角 Y 坐标（像素）
    - `width`: 宽度（像素）
    - `height`: 高度（像素）
  - `href`: 原始链接地址
  - `type`: 链接类型
    - `"internal"`: 内部跳转（`#` 开头）
    - `"external"`: 外部链接（`http://` 或 `https://` 开头）
  - `target`: 目标位置（仅 `type="internal"` 时存在）
    - `section`: 目标章节索引（从 **0** 开始，对应目录 000/, 001/, ...）
    - `page`: 目标页码（从 **1** 开始，对应文件 001.png, 002.png, ...）

**设备端处理建议**：

1. **内部链接**：
   - 读取 `target.section` 和 `target.page`
   - 跳转到对应章节和页面
   - 在屏幕上绘制链接区域的下划线或高亮框

2. **外部链接**：
   - 保留 `href` 信息
   - 设备端不执行跳转（无网络功能）
   - 可选：在屏幕上显示 "外部链接" 标记

3. **触摸检测**：
   - 用户触摸屏幕时，检测触摸点是否在任何 `rect` 区域内
   - 如果在内部链接区域，执行跳转
   - 如果在外部链接区域，显示提示信息

## 页面裁剪方案设计

### 设计背景

1. **屏幕布局**: 屏幕总高度 960px，底部预留 60px 用于 UI（进度条/页码等），实际内容显示区域为 **540 × 900 像素**
2. **阅读连贯性**: 为了避免翻页时丢失上下文，相邻页面之间需要有内容重叠
3. **EPUB 特性**: EPUB 章节渲染后通常是长图，需要智能分页

### 分页参数

```
屏幕宽度:         540px
屏幕高度:         960px
UI 预留高度:       60px
页面内容高度:     900px
页面重叠高度:     100px  （约 11% 重叠，2-3 行文字）
有效步进高度:     800px  （900 - 100）
```

### 分页算法

```
原始长图高度: H 像素
第 N 页的裁剪起始位置: (N - 1) × 800
第 N 页的裁剪高度: 900 像素
总页数: ceil((H - 100) / 800) 或 ceil(H / 800)（取较大值确保覆盖）
```

### 分页示例

假设某章节渲染后的长图高度为 2500 像素：

```
页面1: 裁剪区域 [0, 900)      → 001.png
页面2: 裁剪区域 [800, 1700)   → 002.png  (与页面1重叠100px)
页面3: 裁剪区域 [1600, 2500)  → 003.png  (与页面2重叠100px)
页面4: 裁剪区域 [2400, 2500)  → 004.png  (不足900px，底部填充白色)
```

### 视觉效果

```
┌─────────────────┐
│   页面 1        │  ← 显示 0-900px
│                 │
│   ┌─────────┐   │
│   │ 重叠区  │   │  ← 800-900px (将在下一页顶部重复出现)
└───┴─────────┴───┘

┌───┬─────────┬───┐
│   │ 重叠区  │   │  ← 800-900px (与上一页底部相同内容)
│   └─────────┘   │
│   页面 2        │  ← 显示 800-1700px
│                 │
│   ┌─────────┐   │
│   │ 重叠区  │   │  ← 1600-1700px
└───┴─────────┴───┘
```

### 末页处理

如果末页内容不足 900 像素：
- 底部填充**白色** (#FFFFFF / 灰度 255)
- 保持 540 × 900 的固定尺寸

## PNG 格式要求（重要）

为避免看门狗超时，PNG 必须满足：

| 要求 | 值 | 原因 |
|------|-----|------|
| 尺寸 | 540 × 900 | 固定尺寸，无需缩放 |
| 色彩模式 | 灰度 8-bit | 解码更快，无需颜色转换 |
| Alpha 通道 | **禁止** | 减少数据量和处理时间 |
| 压缩级别 | 9（最高） | 减小文件体积 |
| 文件大小 | < 80KB | 确保解码时间 < 3秒 |

### Python 生成示例

```python
from PIL import Image
import os

# 分页参数
PAGE_WIDTH = 540
PAGE_HEIGHT = 900
OVERLAP = 100
STEP = PAGE_HEIGHT - OVERLAP  # 800

def split_chapter_to_pages(chapter_image_path, output_dir):
    """
    将章节长图分割为多页 PNG
    
    Args:
        chapter_image_path: 章节渲染后的长图路径
        output_dir: 输出目录，如 /sdcard/books/xxx/sections/000/（章节从0开始）
    """
    # 打开并转换为灰度
    img = Image.open(chapter_image_path).convert('L')
    
    # 确保宽度为 540
    if img.width != PAGE_WIDTH:
        ratio = PAGE_WIDTH / img.width
        new_height = int(img.height * ratio)
        img = img.resize((PAGE_WIDTH, new_height), Image.Resampling.LANCZOS)
    
  
  // 加载链接信息（如果存在）
  linksPath = /sdcard/books/{id}/sections/{section:03d}/links.json
  if (fileExists(linksPath)):
    links = loadLinksForPage(linksPath, currentPage)
    drawLinkIndicators(links)  // 在链接下方绘制下划线或边框

触摸事件处理:
  if (userTouch(x, y)):
    for link in currentPageLinks:
      if (x >= link.rect.x && x <= link.rect.x + link.rect.width &&
          y >= link.rect.y && y <= link.rect.y + link.rect.height):
        if (link.type == "internal" && link.target):
          currentSection = link.target.section
          currentPage = link.target.page
          loadPage()
          return
        else if (link.type == "external"):
          showMessage("外部链接不支持")
          return
```

### 链接功能实现要点

1. **内存优化**：
   - 只加载当前页面的链接信息，不要一次性加载整本书的链接
   - 使用 ArduinoJson 的流式解析，避免加载整个 JSON 到内存

2. **视觉反馈**：
   - 在链接文本下方绘制细下划线（1-2 像素）
   - 或者在链接周围绘制虚线边框
   - 使用较浅的灰度值（如 200）避免过于突兀

3. **触摸精度**：
   - 链接的 `rect` 区域已经考虑了文本的实际渲染位置
   - 建议在 Y 轴方向上扩展 5-10 像素的容错空间，提升点击体验

4. **跳转历史**（可选）：
   - 实现一个简单的"返回"功能
   - 记录跳转前的位置（section, page）
   - 按特定按钮可返回上一个阅读位置 total_height = img.height
    page_num = 1
    y_offset = 0
    
    os.makedirs(output_dir, exist_ok=True)
    
    while y_offset < total_height:
        # 创建 540x900 的白色画布
        page = Image.new('L', (PAGE_WIDTH, PAGE_HEIGHT), 255)
        
        # 计算本页需要裁剪的区域
        crop_height = min(PAGE_HEIGHT, total_height - y_offset)
        cropped = img.crop((0, y_offset, PAGE_WIDTH, y_offset + crop_height))
        
        # 粘贴到页面顶部
        page.paste(cropped, (0, 0))
        
        # 保存为高压缩 PNG
        page_path = os.path.join(output_dir, f'{page_num:03d}.png')
        page.save(page_path, 'PNG', optimize=True, compress_level=9)
        
        # 检查文件大小
        size_kb = os.path.getsize(page_path) / 1024
        if size_kb > 80:
            print(f"警告: {page_path} 大小为 {size_kb:.1f}KB，超过 80KB 限制！")
        
        page_num += 1
        y_offset += STEP  # 步进 800，保留 100 重叠
    
    return page_num - 1  # 返回总页数
```

## 上传工具处理流程

前端转换工具（`EpubToImages.tsx`）已实现以下完整流程：

```
1. 解析 EPUB 文件 (epub.js)
        ↓
2. 提取封面 → 裁切为 540×540 正方形 → 转换为灰度 PNG
        ↓
3. 遍历每个章节 (spine):
   a. 渲染章节内容为长图（宽度 540px）
   b. 转换为 16 级灰度
   c. 按 800px 步进分页（900px 高度，100px 重叠）
   d. 每页保存为灰度 PNG
        ↓
4. 生成 metadata.json（包含章节信息和页数）
        ↓
5. 生成 reading_status.json（初始阅读状态）
        ↓
6. 上传到设备 SD 卡
```

### 关键代码参数

```typescript
// 分页参数 (EpubToImages.tsx)
const SCREEN_WIDTH = 540;
const PAGE_HEIGHT = 900;      // 页面内容高度（960 - 60 UI预留）
const OVERLAP_HEIGHT = 100;   // 页面重叠高度（约2-3行文字）
const STEP_HEIGHT = PAGE_HEIGHT - OVERLAP_HEIGHT;  // 800px 步进
```

## 设备端阅读逻辑

```
翻到下一页:
  if (currentPage < currentSection.pageCount):
    currentPage++
  else if (currentSection < totalSections - 1):  // 章节索引从0开始
    currentSection++
    currentPage = 1
    
翻到上一页:
  if (currentPage > 1):
    currentPage--
  else if (currentSection > 0):  // 最小章节索引为0
    currentSection--
    currentPage = sections[currentSection].pageCount  // 直接通过索引查找

加载页面:
  path = /sdcard/books/{id}/sections/{section:03d}/{page:03d}.png
  
  // 检查是否有链接信息文件（同时获取图片标志）
  linksPath = /sdcard/books/{id}/sections/{section:03d}/links.json
  hasImage = false  // 默认无图片
  
  if (fileExists(linksPath)):
    linksData = loadJson(linksPath)
    pageData = linksData.pages.find(p => p.page == currentPage)
    if (pageData):
      hasImage = pageData.hasImage
      links = pageData.links
  
  // 根据是否有图片选择刷新模式
  if (hasImage):
    display.setRefreshMode(GC16)  // 高质量刷新，适合图片
  else:
    display.setRefreshMode(DU)    // 快速刷新，适合纯文本
  
  display.drawPng(path, 0, 0)  // 直接绘制到 (0,0)，无需裁剪
  
  // 如果有链接，绘制链接指示器
  if (links && links.length > 0):
    drawLinkIndicators(links)

触摸事件处理:
  if (userTouch(x, y)):
    for link in currentPageLinks:
      if (x >= link.rect.x && x <= link.rect.x + link.rect.width &&
          y >= link.rect.y && y <= link.rect.y + link.rect.height):
        if (link.type == "internal" && link.target):
          currentSection = link.target.section
          currentPage = link.target.page
          loadPage()
          return
        else if (link.type == "external"):
          showMessage("外部链接不支持")
          return
```

### 刷新模式优化

利用 `hasImage` 标志可以显著提升用户体验：

| 页面类型 | hasImage | 推荐刷新模式 | 效果 |
|---------|----------|-------------|------|
| 纯文本 | `false` | DU/A2 | 快速翻页（~300ms），适合文字阅读 |
| 含图片 | `true` | GC16 | 高质量显示（~800ms），保证图片清晰 |

**实现示例（ESP32 + M5EPD）**：

```cpp
// 加载页面元数据
bool hasImage = false;
if (loadPageMetadata(bookId, section, page, &hasImage)) {
    if (hasImage) {
        M5.EPD.SetRotation(90);
        M5.EPD.Clear(true);  // 全刷新
    } else {
        M5.EPD.SetRotation(90);
        M5.EPD.Clear(false);  // 快速刷新
    }
}
```

## 联系方式

如有疑问请联系设备端开发人员讨论。
