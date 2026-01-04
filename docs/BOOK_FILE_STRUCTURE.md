# M5PaperS3 图书文件结构规范

## 概述

本文档定义了 M5PaperS3 电子书阅读器的图书存储结构。所有图书数据存储在 SD 卡的 `/books/` 目录下，每本图书占用一个独立的子目录。

---

## 目录结构

```
/books/
├── book_1735862400000_xyz123/          # 图书目录（格式：book_{timestamp}_{random}）
│   ├── COVER.png                       # 封面图片（正方形，16级灰度）
│   ├── metadata.json                   # 图书元数据
│   ├── reading_status.json             # 阅读进度状态
│   ├── 001_第一章_引子.png              # 章节图片
│   ├── 002_第二章_启程.png
│   ├── 003_第三章_相遇.png
│   └── ...
├── book_1735862500000_abc456/          # 另一本图书
│   ├── COVER.png
│   ├── metadata.json
│   ├── reading_status.json
│   └── ...
└── ...
```

---

## 文件规范

### 1. 图书目录命名

**格式**: `book_{timestamp}_{random}`

- `timestamp`: Unix 时间戳（毫秒）
- `random`: 9位随机字符串（Base36）

**示例**: `book_1735862400000_xyz123abc`

**用途**: 确保每本图书有唯一的目录名，避免冲突

---

### 2. COVER.png（封面图片）

**规格**:
- **格式**: PNG
- **尺寸**: 正方形（推荐 540×540）
- **色彩**: 16级灰度（0, 17, 34, 51, ... 255）
- **大小**: 通常 < 200KB

**说明**:
- 封面用于书架展示
- 如果原图不是正方形，从左上角裁切短边
- 必须经过灰度量化处理以适配墨水屏

---

### 3. metadata.json（图书元数据）

**格式**: JSON

**字段说明**:

```json
{
  "id": "book_1735862400000_xyz123",      // 图书ID（与目录名一致）
  "title": "西游记",                       // 书名（必填）
  "author": "吴承恩",                      // 作者（可选）
  "chapters": 100,                        // 章节总数（必填）
  "addedAt": "2026-01-03T08:30:00.000Z"  // 添加时间 ISO8601（必填）
}
```

**字段详解**:

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `id` | string | ✅ | 图书唯一标识，与目录名相同 |
| `title` | string | ✅ | 图书标题，用于显示 |
| `author` | string | ⭕ | 作者名称 |
| `chapters` | number | ✅ | 章节总数，用于进度计算 |
| `addedAt` | string | ✅ | 添加时间，ISO8601 格式 |

**注意事项**:
- 所有字符串使用 UTF-8 编码
- `addedAt` 用于书架排序（最新添加的在前）
- 设备端应该只读此文件，不应修改

---

### 4. reading_status.json（阅读状态）

**格式**: JSON

**字段说明**:

```json
{
  "currentChapter": 5,                    // 当前阅读的章节索引（从0开始）
  "currentOffset": 1200,                  // 当前章节内的阅读位置（像素）
  "lastReadTime": "2026-01-03T10:15:30.000Z",  // 最后阅读时间
  "chapters": [                           // 章节信息列表
    {
      "index": 0,                        // 章节索引
      "title": "第一章 引子",             // 章节标题
      "height": 5400,                    // 章节图片高度（像素）
      "filename": "001_第一章_引子.png"   // 文件名
    },
    {
      "index": 1,
      "title": "第二章 启程",
      "height": 6200,
      "filename": "002_第二章_启程.png"
    },
    {
      "index": 2,
      "title": "第三章 相遇",
      "height": 5800,
      "filename": "003_第三章_相遇.png"
    }
    // ... 更多章节
  ]
}
```

**字段详解**:

#### 顶层字段

| 字段 | 类型 | 说明 |
|------|------|------|
| `currentChapter` | number | 当前正在阅读的章节索引（从0开始） |
| `currentOffset` | number | 当前章节内的垂直滚动位置（像素，从顶部计算） |
| `lastReadTime` | string | 最后一次阅读的时间戳（ISO8601格式） |
| `chapters` | array | 所有章节的详细信息 |

#### chapters 数组元素

| 字段 | 类型 | 说明 |
|------|------|------|
| `index` | number | 章节索引（从0开始，对应数组下标） |
| `title` | string | 章节标题（从EPUB提取或自动生成） |
| `height` | number | 章节图片的实际高度（像素） |
| `filename` | string | 章节图片的文件名 |

**使用场景**:

1. **打开图书时**: 
   - 读取 `currentChapter` 和 `currentOffset`
   - 加载对应章节图片
   - 滚动到 `currentOffset` 位置

2. **翻页时**:
   - 根据 `chapters[].height` 计算是否需要换章节
   - 更新 `currentChapter` 和 `currentOffset`

3. **退出阅读时**:
   - 保存当前的 `currentChapter` 和 `currentOffset`
   - 更新 `lastReadTime`

**注意事项**:
- 设备端需要频繁读写此文件
- 建议在内存中维护状态，定期写入文件（如每次翻页或退出时）
- `currentOffset` 的范围: `0 <= currentOffset < chapters[currentChapter].height`

---

### 5. 章节图片文件

**命名格式**: `{index:03d}_{title}.png`

**示例**:
- `001_第一章_引子.png`
- `002_第二章_启程.png`
- `023_第二十三章_决战.png`

**规格**:
- **格式**: PNG
- **宽度**: 540 像素（固定）
- **高度**: 可变（取决于内容长度）
- **色彩**: 16级灰度
- **大小**: 通常 100KB - 2MB/章

**命名规则**:
- 前缀 3 位数字（001, 002, ...）确保文件系统按顺序排列
- 下划线分隔索引和标题
- 标题中的特殊字符被替换为下划线
- 扩展名统一为 `.png`

**注意事项**:
- 索引从 001 开始（不是 000）
- 标题可能包含中文、日文、韩文等 Unicode 字符
- 不应依赖文件名排序，应使用 `reading_status.json` 中的 `chapters` 数组

---

## 设备端实现建议

### 初始化加载流程

```
1. 扫描 /books/ 目录，列出所有子目录
2. 对每个子目录：
   a. 读取 metadata.json 获取图书信息
   b. 读取 COVER.png 缩略图
   c. 构建书架列表
3. 按 addedAt 时间戳排序显示
```

### 打开图书流程

```
1. 读取 reading_status.json
2. 加载 chapters[currentChapter].filename 对应的图片
3. 显示图片并滚动到 currentOffset 位置
4. 在内存中保存 reading_status 对象
```

### 翻页逻辑

**向下翻页**:
```
1. currentOffset += SCREEN_HEIGHT (例如 960)
2. 如果 currentOffset >= chapters[currentChapter].height:
   a. currentChapter += 1
   b. currentOffset = 0
   c. 加载新章节图片
3. 否则：滚动当前图片
4. 更新 lastReadTime
5. 写入 reading_status.json
```

**向上翻页**:
```
1. currentOffset -= SCREEN_HEIGHT
2. 如果 currentOffset < 0:
   a. currentChapter -= 1
   b. currentOffset = chapters[currentChapter].height - SCREEN_HEIGHT
   c. 加载新章节图片
3. 否则：滚动当前图片
4. 更新 lastReadTime
5. 写入 reading_status.json
```

### 阅读进度计算

```cpp
// 计算全书阅读百分比
float getReadingProgress(ReadingStatus status) {
    int totalHeight = 0;
    int currentHeight = 0;
    
    for (int i = 0; i < status.chapters.length; i++) {
        totalHeight += status.chapters[i].height;
        if (i < status.currentChapter) {
            currentHeight += status.chapters[i].height;
        }
    }
    
    currentHeight += status.currentOffset;
    
    return (float)currentHeight / (float)totalHeight * 100.0f;
}
```

### 文件读写优化

1. **缓存策略**:
   - 将 `metadata.json` 缓存到内存（启动时读取）
   - 将 `reading_status.json` 缓存到内存（打开图书时读取）
   - 章节图片按需加载，预加载前后各1章

2. **写入时机**:
   - 每次翻页后立即写入 `reading_status.json`
   - 或每隔 5 秒自动保存一次
   - 退出阅读时必须写入

3. **错误处理**:
   - 如果 `reading_status.json` 损坏，重置为第一章
   - 如果章节文件缺失，跳过该章节

---

## 数据示例

### 完整示例：一本书的文件结构

```
/books/book_1735862400000_xyz123/
├── COVER.png                          (45 KB, 540×540)
├── metadata.json                      (200 bytes)
├── reading_status.json                (5 KB)
├── 001_序章.png                       (320 KB, 540×3200)
├── 002_第一章_相遇.png                 (450 KB, 540×4800)
├── 003_第二章_启程.png                 (580 KB, 540×6200)
├── 004_第三章_试炼.png                 (510 KB, 540×5400)
└── ...
```

### metadata.json 示例

```json
{
  "id": "book_1735862400000_xyz123",
  "title": "修仙模拟器",
  "author": "张三",
  "chapters": 120,
  "addedAt": "2026-01-03T08:30:00.000Z"
}
```

### reading_status.json 示例（简化）

```json
{
  "currentChapter": 2,
  "currentOffset": 1920,
  "lastReadTime": "2026-01-03T14:25:30.000Z",
  "chapters": [
    {
      "index": 0,
      "title": "序章",
      "height": 3200,
      "filename": "001_序章.png"
    },
    {
      "index": 1,
      "title": "第一章 相遇",
      "height": 4800,
      "filename": "002_第一章_相遇.png"
    },
    {
      "index": 2,
      "title": "第二章 启程",
      "height": 6200,
      "filename": "003_第二章_启程.png"
    }
  ]
}
```

---

## API 交互

### 上传图书（前端 → 设备）

前端会按以下顺序上传文件：

1. **创建目录**: `POST /api/mkdir?path=/books/{bookId}`
2. **上传封面**: `POST /api/file?path=/books/{bookId}/COVER.png`
3. **上传章节**: 
   - `POST /api/file?path=/books/{bookId}/001_xxx.png`
   - `POST /api/file?path=/books/{bookId}/002_xxx.png`
   - ...
4. **上传阅读状态**: `POST /api/file?path=/books/{bookId}/reading_status.json`
5. **上传元数据**: `POST /api/file?path=/books/{bookId}/metadata.json`

### 获取图书列表（设备 → 前端）

前端通过以下步骤获取书架：

1. **列出图书目录**: `GET /api/list?path=/books`
2. **对每本书**:
   - `GET /api/file?path=/books/{bookId}/metadata.json`
   - `GET /api/file?path=/books/{bookId}/COVER.png`

### 更新阅读进度（设备 → 前端）

设备端需要在阅读时更新 `reading_status.json`：

```
1. 读取当前 reading_status.json
2. 修改 currentChapter, currentOffset, lastReadTime
3. 上传更新: POST /api/file?path=/books/{bookId}/reading_status.json
```

---

## 注意事项

### 1. 文件编码
- 所有 JSON 文件使用 **UTF-8 编码**
- 文件名支持 Unicode（中文、日文、韩文等）

### 2. 错误处理
- 如果 `reading_status.json` 不存在或损坏，重置到第一章
- 如果章节图片缺失，跳过该章节或显示错误提示
- 建议定期备份 `reading_status.json`

### 3. 性能优化
- 预加载相邻章节图片（当前章节 ±1）
- 缓存已读章节的元数据
- 使用增量更新 `reading_status.json`

### 4. 存储空间管理
- 一本 100 章的书约占用 50-100 MB
- 建议支持删除图书功能，释放空间
- 可以实现"已读/未读"标记系统

### 5. 时间同步
- `lastReadTime` 依赖设备时间准确性
- 建议在 WiFi 连接时同步 NTP 时间

---

## 版本历史

- **v1.0.0** (2026-01-03): 初始版本
  - 定义基础文件结构
  - 添加 `reading_status.json`
  - 支持章节高度记录

---

## 参考资料

- HTTP API 文档: `docs/HTTP_API_DOCUMENTATION.md`
- 前端实现: `setting-frontend/src/api/M5PaperS3Client.ts`
- 设备端固件: `main/`

---

## 许可

MIT License - Copyright (c) 2026 M5Stack Technology CO LTD
