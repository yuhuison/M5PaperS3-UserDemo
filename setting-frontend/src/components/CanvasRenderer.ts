/**
 * 纯 Canvas 2D 文本渲染器
 * 直接使用 Canvas API 绘制文本，避免 html2canvas 的内存问题
 */

// 渲染配置
export interface RenderConfig {
  fontFamily: string;
  fontSize: number;
  fontWeight: number;
  lineHeight: number;
  paddingH: number;
  paddingV: number;
  textColor: string;
  backgroundColor: string;
  imageGrayscale: boolean;
  grayscaleLevels: number;
}

// 页面尺寸常量
export const PAGE_WIDTH = 540;
export const PAGE_HEIGHT = 900;
export const OVERLAP_HEIGHT = 100;
export const STEP_HEIGHT = PAGE_HEIGHT - OVERLAP_HEIGHT;

// 解析后的内容块
interface ContentBlock {
  type: 'text' | 'image' | 'heading' | 'paragraph' | 'break' | 'link';
  content?: string;
  level?: number;  // 标题级别 1-6
  src?: string;    // 图片 data URL
  width?: number;
  height?: number;
  href?: string;   // 链接地址
  linkText?: string; // 链接文本
}

// 链接信息
export interface LinkInfo {
  text: string;
  rect: {
    x: number;
    y: number;
    width: number;
    height: number;
  };
  href: string;
  type: 'internal' | 'external';
  target?: {
    section: number;
    page: number;
  };
}

// 页面链接信息
export interface PageLinks {
  page: number;
  hasImage: boolean;  // 该页是否包含图片
  links: LinkInfo[];
}

// 渲染结果
export interface RenderResult {
  pages: Blob[];
  pageLinks: PageLinks[];
  anchors: Map<string, { page: number }>;  // 锚点映射到页码（相对于当前章节）
}

/**
 * 从 HTML 解析内容块（包含链接信息）
 */
export function parseHtmlToBlocks(html: string): ContentBlock[] {
  const parser = new DOMParser();
  const doc = parser.parseFromString(html, 'text/html');
  const blocks: ContentBlock[] = [];
  
  function processNode(node: Node, inLink: boolean = false, linkHref: string = ''): void {
    if (node.nodeType === Node.TEXT_NODE) {
      const text = node.textContent?.trim();
      if (text) {
        if (inLink) {
          // 在链接内的文本
          blocks.push({ 
            type: 'link', 
            content: text,
            href: linkHref,
            linkText: text
          });
        } else {
          blocks.push({ type: 'text', content: text });
        }
      }
      return;
    }
    
    if (node.nodeType !== Node.ELEMENT_NODE) return;
    
    const el = node as Element;
    const tagName = el.tagName.toLowerCase();
    
    // 处理链接
    if (tagName === 'a') {
      const href = el.getAttribute('href');
      if (href) {
        // 递归处理链接内的内容
        for (const child of Array.from(node.childNodes)) {
          processNode(child, true, href);
        }
      }
      return;
    }
    
    // 处理标题
    if (/^h[1-6]$/.test(tagName)) {
      const level = parseInt(tagName[1]);
      const text = el.textContent?.trim();
      if (text) {
        blocks.push({ type: 'heading', content: text, level });
      }
      return;
    }
    
    // 处理图片
    if (tagName === 'img') {
      const src = el.getAttribute('src');
      if (src) {
        blocks.push({ type: 'image', src });
      }
      return;
    }
    
    // 处理 SVG 元素（需要递归处理内部的 image 标签）
    if (tagName === 'svg') {
      for (const child of Array.from(node.childNodes)) {
        processNode(child, inLink, linkHref);
      }
      return;
    }
    
    // 处理 SVG image 元素
    if (tagName === 'image') {
      const src = el.getAttribute('href') || el.getAttribute('xlink:href');
      if (src) {
        blocks.push({ type: 'image', src });
      }
      return;
    }
    
    // 处理段落和 div
    if (tagName === 'p' || tagName === 'div') {
      // 先处理子节点
      for (const child of Array.from(node.childNodes)) {
        processNode(child, inLink, linkHref);
      }
      // 段落后添加换行
      blocks.push({ type: 'break' });
      return;
    }
    
    // 处理换行
    if (tagName === 'br') {
      blocks.push({ type: 'break' });
      return;
    }
    
    // 递归处理其他元素的子节点
    for (const child of Array.from(node.childNodes)) {
      processNode(child, inLink, linkHref);
    }
  }
  
  processNode(doc.body);
  return blocks;
}

/**
 * 将文本按宽度换行
 */
function wrapText(
  ctx: CanvasRenderingContext2D,
  text: string,
  maxWidth: number
): string[] {
  const lines: string[] = [];
  
  // 按段落分割
  const paragraphs = text.split(/\n/);
  
  for (const paragraph of paragraphs) {
    if (!paragraph.trim()) {
      lines.push('');
      continue;
    }
    
    let currentLine = '';
    
    // 逐字符处理（支持中文）
    for (const char of paragraph) {
      const testLine = currentLine + char;
      const metrics = ctx.measureText(testLine);
      
      if (metrics.width > maxWidth && currentLine) {
        lines.push(currentLine);
        currentLine = char;
      } else {
        currentLine = testLine;
      }
    }
    
    if (currentLine) {
      lines.push(currentLine);
    }
  }
  
  return lines;
}

/**
 * 加载图片
 */
async function loadImage(src: string): Promise<HTMLImageElement> {
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.onload = () => resolve(img);
    img.onerror = reject;
    img.src = src;
  });
}

/**
 * 对画布应用灰度转换
 */
function applyGrayscale(
  ctx: CanvasRenderingContext2D,
  width: number,
  height: number,
  levels: number
) {
  const imageData = ctx.getImageData(0, 0, width, height);
  const data = imageData.data;
  const step = 256 / levels;
  
  for (let i = 0; i < data.length; i += 4) {
    const gray = 0.299 * data[i] + 0.587 * data[i + 1] + 0.114 * data[i + 2];
    const quantized = Math.min(255, Math.round(gray / step) * step);
    data[i] = quantized;
    data[i + 1] = quantized;
    data[i + 2] = quantized;
  }
  
  ctx.putImageData(imageData, 0, 0);
}

/**
 * 渲染内容到多个页面（返回 Blob 数组和链接信息）
 */
export async function renderToPages(
  html: string,
  config: RenderConfig,
  onProgress?: (message: string) => void
): Promise<RenderResult> {
  const blocks = parseHtmlToBlocks(html);
  const pages: Blob[] = [];
  const pageLinks: PageLinks[] = [];
  const anchors = new Map<string, { page: number }>();
  
  const contentWidth = PAGE_WIDTH - config.paddingH * 2;
  
  // 创建临时 canvas 用于测量
  const measureCanvas = document.createElement('canvas');
  measureCanvas.width = PAGE_WIDTH;
  measureCanvas.height = PAGE_HEIGHT;
  const measureCtx = measureCanvas.getContext('2d')!;
  
  // 预处理：将所有内容转换为行
  interface RenderLine {
    type: 'text' | 'image' | 'space' | 'link';
    content?: string;
    fontSize?: number;
    fontWeight?: number;
    height: number;
    image?: HTMLImageElement;
    imageWidth?: number;
    imageHeight?: number;
    href?: string;       // 链接地址
    linkText?: string;   // 链接文本
  }
  
  const allLines: RenderLine[] = [];
  const baseFontSize = config.fontSize;
  const lineHeightPx = baseFontSize * config.lineHeight;
  
  onProgress?.('解析内容...');
  
  for (const block of blocks) {
    if (block.type === 'break') {
      allLines.push({ type: 'space', height: lineHeightPx * 0.5 });
      continue;
    }
    
    if (block.type === 'heading' && block.content) {
      const level = block.level || 1;
      const headingSize = baseFontSize * (1.5 - level * 0.1);
      
      measureCtx.font = `bold ${headingSize}px ${config.fontFamily}`;
      const lines = wrapText(measureCtx, block.content, contentWidth);
      
      // 标题前空行
      allLines.push({ type: 'space', height: lineHeightPx });
      
      for (const line of lines) {
        allLines.push({
          type: 'text',
          content: line,
          fontSize: headingSize,
          fontWeight: 700,
          height: headingSize * config.lineHeight,
        });
      }
      
      // 标题后空行
      allLines.push({ type: 'space', height: lineHeightPx * 0.5 });
      continue;
    }
    
    if (block.type === 'text' && block.content) {
      measureCtx.font = `${config.fontWeight} ${baseFontSize}px ${config.fontFamily}`;
      const lines = wrapText(measureCtx, block.content, contentWidth);
      
      for (const line of lines) {
        allLines.push({
          type: 'text',
          content: line,
          fontSize: baseFontSize,
          fontWeight: config.fontWeight,
          height: lineHeightPx,
        });
      }
      continue;
    }
    
    if (block.type === 'link' && block.content) {
      measureCtx.font = `${config.fontWeight} ${baseFontSize}px ${config.fontFamily}`;
      const lines = wrapText(measureCtx, block.content, contentWidth);
      
      for (const line of lines) {
        allLines.push({
          type: 'link',
          content: line,
          fontSize: baseFontSize,
          fontWeight: config.fontWeight,
          height: lineHeightPx,
          href: block.href,
          linkText: block.linkText,
        });
      }
      continue;
    }
    
    if (block.type === 'image' && block.src) {
      try {
        const img = await loadImage(block.src);
        // 缩放图片以适应宽度
        let imgWidth = img.width;
        let imgHeight = img.height;
        
        if (imgWidth > contentWidth) {
          const ratio = contentWidth / imgWidth;
          imgWidth = contentWidth;
          imgHeight = img.height * ratio;
        }
        
        // 图片前后空行
        allLines.push({ type: 'space', height: lineHeightPx * 0.5 });
        allLines.push({
          type: 'image',
          height: imgHeight,
          image: img,
          imageWidth: imgWidth,
          imageHeight: imgHeight,
        });
        allLines.push({ type: 'space', height: lineHeightPx * 0.5 });
      } catch (e) {
        console.warn('图片加载失败:', block.src);
      }
      continue;
    }
  }
  
  onProgress?.(`共 ${allLines.length} 行内容，开始分页...`);
  
  // 分页渲染
  let lineIndex = 0;
  let pageNum = 0;
  
  while (lineIndex < allLines.length) {
    pageNum++;
    onProgress?.(`渲染第 ${pageNum} 页...`);
    
    // 当前页面的链接列表和图片标志
    const currentPageLinks: LinkInfo[] = [];
    let pageHasImage = false;
    
    // 创建新页面
    const pageCanvas = document.createElement('canvas');
    pageCanvas.width = PAGE_WIDTH;
    pageCanvas.height = PAGE_HEIGHT;
    const ctx = pageCanvas.getContext('2d')!;
    
    // 填充背景
    ctx.fillStyle = config.backgroundColor;
    ctx.fillRect(0, 0, PAGE_WIDTH, PAGE_HEIGHT);
    
    // 设置文本样式
    ctx.fillStyle = config.textColor;
    ctx.textBaseline = 'top';
    
    let y = config.paddingV;
    const maxY = PAGE_HEIGHT - config.paddingV;
    
    // 渲染行直到页面填满
    while (lineIndex < allLines.length && y < maxY) {
      const line = allLines[lineIndex];
      
      // 检查是否能放下这一行
      if (y + line.height > maxY) {
        // 如果是第一行都放不下，强制放入（避免死循环）
        if (y === config.paddingV) {
          lineIndex++;
        }
        break;
      }
      
      if (line.type === 'space') {
        y += line.height;
        lineIndex++;
        continue;
      }
      
      if (line.type === 'text' && line.content) {
        ctx.font = `${line.fontWeight} ${line.fontSize}px ${config.fontFamily}`;
        ctx.fillText(line.content, config.paddingH, y);
        y += line.height;
        lineIndex++;
        continue;
      }
      
      if (line.type === 'link' && line.content && line.href) {
        ctx.font = `${line.fontWeight} ${line.fontSize}px ${config.fontFamily}`;
        const textWidth = ctx.measureText(line.content).width;
        
        // 绘制链接文本
        ctx.fillText(line.content, config.paddingH, y);
        
        // 绘制下划线（可选）
        ctx.strokeStyle = config.textColor;
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(config.paddingH, y + line.height! - 2);
        ctx.lineTo(config.paddingH + textWidth, y + line.height! - 2);
        ctx.stroke();
        
        // 记录链接信息
        const linkType = line.href.startsWith('#') ? 'internal' : 
                        (line.href.startsWith('http://') || line.href.startsWith('https://')) ? 'external' : 
                        'internal';
        
        currentPageLinks.push({
          text: line.linkText?.substring(0, 100) || line.content.substring(0, 100),
          rect: {
            x: config.paddingH,
            y: y,
            width: textWidth,
            height: line.height!
          },
          href: line.href,
          type: linkType
          // target 将在后续处理中填充
        });
        
        y += line.height;
        lineIndex++;
        continue;
      }
      
      if (line.type === 'image' && line.image) {
        const x = config.paddingH + (contentWidth - line.imageWidth!) / 2;
        ctx.drawImage(line.image, x, y, line.imageWidth!, line.imageHeight!);
        pageHasImage = true;  // 标记该页有图片
        y += line.height;
        lineIndex++;
        continue;
      }
      
      lineIndex++;
    }
    
    // 应用灰度转换
    if (config.imageGrayscale) {
      applyGrayscale(ctx, PAGE_WIDTH, PAGE_HEIGHT, config.grayscaleLevels);
    }
    
    // 使用 toBlob 替代 toDataURL，压缩效果更好
    // 对于二值图（2级灰度），PNG 压缩率极高
    const blob = await new Promise<Blob>((resolve, reject) => {
      pageCanvas.toBlob(
        (b) => {
          if (b) resolve(b);
          else reject(new Error('转换 Blob 失败'));
        },
        'image/png',
        1.0  // PNG 质量参数（虽然 PNG 无损，但某些浏览器会优化）
      );
    });
    
    pages.push(blob);
    
    // 保存当前页面的元数据（链接和图片标志）
    // 注意：即使没有链接，也要保存 hasImage 信息
    pageLinks.push({
      page: pageNum,
      hasImage: pageHasImage,
      links: currentPageLinks
    });
  }
  
  onProgress?.(`完成，共 ${pages.length} 页`);
  return {
    pages,
    pageLinks,
    anchors
  };
}
