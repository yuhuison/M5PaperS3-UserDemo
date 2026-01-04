/**
 * EpubToImages - EPUB 电子书转换为 PNG 图片组件
 * 
 * 流程：
 * 1. 加载 EPUB 解析章节
 * 2. 点击"开始转换"渲染所有页面（可预览）
 * 3. 确认后点击"上传到设备"
 */

import { useEffect, useRef, useState } from 'react';
import ePub, { Book, NavItem } from 'epubjs';
import type { IDeviceClient } from '../api/index';
import './EpubToImages.css';

// M5PaperS3 屏幕尺寸
const SCREEN_WIDTH = 540;
const SCREEN_HEIGHT = 960;
const CONTENT_HEIGHT = 900;

interface EpubToImagesProps {
  file: File;
  onClose: () => void;
  onComplete?: () => void;
  client?: IDeviceClient;
}

interface RenderConfig {
  fontFamily: string;
  fontSize: number;
  lineHeight: number;
  paddingH: number;
  paddingV: number;
}

const DEFAULT_CONFIG: RenderConfig = {
  fontFamily: 'serif',
  fontSize: 28,
  lineHeight: 1.6,
  paddingH: 24,
  paddingV: 20,
};

interface ChapterInfo {
  index: number;
  title: string;
  href: string;
}

// 转换后的数据
interface ConvertedBook {
  bookId: string;
  title: string;
  author: string;
  cover: Blob;
  sections: Array<{
    index: number;
    title: string;
    pages: Blob[];
  }>;
  totalPages: number;
}

type ProcessStatus = 'idle' | 'loading' | 'converting' | 'converted' | 'uploading' | 'completed' | 'error';

interface Progress {
  status: ProcessStatus;
  message: string;
  current: number;
  total: number;
}

export function EpubToImages({ file, onClose, onComplete, client }: EpubToImagesProps) {
  const [bookTitle, setBookTitle] = useState<string>('');
  const [bookAuthor, setBookAuthor] = useState<string>('');
  const [chapters, setChapters] = useState<ChapterInfo[]>([]);
  const [config, setConfig] = useState<RenderConfig>(DEFAULT_CONFIG);
  const [progress, setProgress] = useState<Progress>({
    status: 'idle',
    message: '',
    current: 0,
    total: 0,
  });
  
  // 转换结果
  const [convertedBook, setConvertedBook] = useState<ConvertedBook | null>(null);
  const [previewIndex, setPreviewIndex] = useState<number>(0);
  const [allPages, setAllPages] = useState<Array<{ sectionIdx: number; pageIdx: number; blob: Blob }>>([]);
  
  const bookRef = useRef<Book | null>(null);
  const abortRef = useRef<boolean>(false);

  // 生成纯英文数字的 bookId
  const generateBookId = (): string => {
    const timestamp = Date.now().toString(36);
    const random = Math.random().toString(36).substring(2, 8);
    return `book_${timestamp}_${random}`;
  };

  // 加载 EPUB 文件
  useEffect(() => {
    const loadEpub = async () => {
      setProgress({ status: 'loading', message: '正在加载电子书...', current: 0, total: 0 });
      
      try {
        const arrayBuffer = await file.arrayBuffer();
        const book = ePub(arrayBuffer);
        bookRef.current = book;
        
        await book.ready;
        
        const metadata = await book.loaded.metadata;
        console.log('EPUB 元数据:', metadata);
        setBookTitle(metadata.title || file.name.replace('.epub', ''));
        setBookAuthor(metadata.creator || '');
        
        const navigation = await book.loaded.navigation;
        console.log('EPUB 导航:', navigation);
        
        const chapterList: ChapterInfo[] = [];
        const toc = navigation.toc || [];
        
        if (toc.length > 0) {
          const flattenToc = (items: NavItem[], _depth = 0) => {
            items.forEach((item) => {
              chapterList.push({
                index: chapterList.length,
                title: item.label?.trim() || `章节 ${chapterList.length + 1}`,
                href: item.href,
              });
              if (item.subitems && item.subitems.length > 0) {
                flattenToc(item.subitems, _depth + 1);
              }
            });
          };
          flattenToc(toc);
        }
        
        // 如果 TOC 为空，从 spine 获取
        if (chapterList.length === 0) {
          console.log('TOC 为空，从 spine 获取章节');
          const spine = book.spine as any;
          if (spine && spine.items) {
            spine.items.forEach((item: any, idx: number) => {
              chapterList.push({
                index: idx,
                title: item.idref || `章节 ${idx + 1}`,
                href: item.href,
              });
            });
          }
        }
        
        console.log('解析到的章节:', chapterList);
        setChapters(chapterList);
        setProgress({ status: 'idle', message: `已加载 ${chapterList.length} 个章节`, current: 0, total: chapterList.length });
      } catch (error) {
        console.error('加载 EPUB 失败:', error);
        setProgress({ 
          status: 'error', 
          message: `加载失败: ${error instanceof Error ? error.message : '未知错误'}`,
          current: 0, 
          total: 0 
        });
      }
    };
    
    loadEpub();
    
    return () => {
      if (bookRef.current) {
        bookRef.current.destroy();
      }
    };
  }, [file]);

  // 将 blob URL 图片转换为 base64
  const convertImageToBase64 = async (imgSrc: string, book: Book): Promise<string> => {
    try {
      // 如果已经是 base64 或 data URL，直接返回
      if (imgSrc.startsWith('data:')) {
        return imgSrc;
      }
      
      // 使用 epub.js 的资源加载器获取图片
      const archive = (book as any).archive;
      if (!archive) {
        console.warn('无法访问 EPUB archive');
        return imgSrc;
      }
      
      // 处理相对路径
      let imagePath = imgSrc;
      if (imgSrc.startsWith('blob:')) {
        // blob URL 无法直接处理，跳过
        return imgSrc;
      }
      
      // 尝试从 archive 加载图片
      const imageBlob = await archive.getBlob(imagePath);
      if (imageBlob) {
        return new Promise<string>((resolve) => {
          const reader = new FileReader();
          reader.onloadend = () => resolve(reader.result as string);
          reader.onerror = () => resolve(imgSrc);
          reader.readAsDataURL(imageBlob);
        });
      }
      
      return imgSrc;
    } catch (error) {
      console.warn('转换图片失败:', imgSrc, error);
      return imgSrc;
    }
  };

  // 获取章节 HTML 并处理图片
  const getChapterHtml = async (book: Book, href: string): Promise<string> => {
    try {
      let section = book.spine.get(href);
      if (!section) {
        const spineItems = book.spine as any;
        for (const item of spineItems.items || []) {
          if (item.href === href || item.href.includes(href) || href.includes(item.href)) {
            section = item;
            break;
          }
        }
      }
      
      if (!section) return '';
      
      await section.load(book.load.bind(book));
      const doc = section.document;
      if (!doc?.body) return '';
      
      // 获取基础路径用于解析相对图片路径
      const sectionHref = (section as any).href || href;
      const basePath = sectionHref.substring(0, sectionHref.lastIndexOf('/') + 1);
      
      // 处理所有图片，转换为 base64
      const images = doc.querySelectorAll('img');
      for (const img of images) {
        const src = img.getAttribute('src');
        if (src) {
          // 构建完整路径
          let fullPath = src;
          if (!src.startsWith('data:') && !src.startsWith('http') && !src.startsWith('blob:')) {
            fullPath = basePath + src;
            // 处理 ../ 相对路径
            fullPath = fullPath.replace(/[^/]+\/\.\.\//g, '');
          }
          
          try {
            const archive = (book as any).archive;
            if (archive) {
              const imageBlob = await archive.getBlob(fullPath);
              if (imageBlob) {
                const base64 = await new Promise<string>((resolve) => {
                  const reader = new FileReader();
                  reader.onloadend = () => resolve(reader.result as string);
                  reader.onerror = () => resolve('');
                  reader.readAsDataURL(imageBlob);
                });
                if (base64) {
                  img.setAttribute('src', base64);
                }
              }
            }
          } catch (e) {
            console.warn('处理图片失败:', fullPath, e);
          }
        }
      }
      
      return doc.body.innerHTML;
    } catch (error) {
      console.warn('获取章节内容失败:', href, error);
      return '';
    }
  };

  // 文本换行
  const wrapText = (ctx: CanvasRenderingContext2D, text: string, maxWidth: number): string[] => {
    const lines: string[] = [];
    const paragraphs = text.split('\n');
    
    for (const paragraph of paragraphs) {
      if (paragraph.trim() === '') {
        lines.push('');
        continue;
      }
      
      let currentLine = '';
      const chars = paragraph.split('');
      
      for (const char of chars) {
        const testLine = currentLine + char;
        const metrics = ctx.measureText(testLine);
        
        if (metrics.width > maxWidth && currentLine !== '') {
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
  };

  // 渲染页面到 Canvas（支持文本和图片）
  const renderPageToBlob = async (
    elements: Element[],
    cfg: RenderConfig
  ): Promise<Blob> => {
    const canvas = document.createElement('canvas');
    canvas.width = SCREEN_WIDTH;
    canvas.height = SCREEN_HEIGHT;
    const ctx = canvas.getContext('2d')!;
    
    ctx.fillStyle = '#ffffff';
    ctx.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    
    ctx.font = `${cfg.fontSize}px ${cfg.fontFamily}`;
    ctx.fillStyle = '#000000';
    ctx.textBaseline = 'top';
    
    let y = cfg.paddingV;
    const lineHeight = cfg.fontSize * cfg.lineHeight;
    const maxWidth = SCREEN_WIDTH - cfg.paddingH * 2;
    
    for (const el of elements) {
      // 检查是否是图片元素
      if (el.tagName === 'IMG') {
        const img = el as HTMLImageElement;
        const src = img.getAttribute('src') || '';
        
        if (src.startsWith('data:')) {
          try {
            // 创建图片对象并绘制
            const imgObj = new Image();
            await new Promise<void>((resolve, reject) => {
              imgObj.onload = () => resolve();
              imgObj.onerror = () => reject(new Error('图片加载失败'));
              imgObj.src = src;
            });
            
            // 计算图片尺寸，限制最大宽度
            let imgWidth = imgObj.width;
            let imgHeight = imgObj.height;
            if (imgWidth > maxWidth) {
              imgHeight = (maxWidth / imgWidth) * imgHeight;
              imgWidth = maxWidth;
            }
            
            // 检查是否超出页面
            if (y + imgHeight > SCREEN_HEIGHT - cfg.paddingV) {
              // 图片太大，跳过或缩放
              const availableHeight = SCREEN_HEIGHT - cfg.paddingV - y;
              if (availableHeight > 50) {
                imgHeight = availableHeight;
                imgWidth = (availableHeight / imgObj.height) * imgObj.width;
              } else {
                continue;
              }
            }
            
            ctx.drawImage(imgObj, cfg.paddingH, y, imgWidth, imgHeight);
            y += imgHeight + lineHeight * 0.5;
          } catch (e) {
            console.warn('绘制图片失败:', e);
          }
        }
      } else {
        // 文本元素
        const text = el.textContent || '';
        if (text.trim()) {
          const lines = wrapText(ctx, text, maxWidth);
          
          for (const line of lines) {
            if (y + lineHeight > SCREEN_HEIGHT - cfg.paddingV) break;
            ctx.fillText(line, cfg.paddingH, y);
            y += lineHeight;
          }
          y += lineHeight * 0.3;
        }
      }
    }
    
    return new Promise<Blob>((resolve) => {
      canvas.toBlob((blob) => resolve(blob!), 'image/png');
    });
  };

  // 渲染 HTML 到多页
  const renderHtmlToPages = async (html: string, cfg: RenderConfig): Promise<Blob[]> => {
    const contentWidth = SCREEN_WIDTH - cfg.paddingH * 2;
    const contentHeight = CONTENT_HEIGHT - cfg.paddingV * 2;
    
    const container = document.createElement('div');
    container.style.cssText = `
      position: fixed;
      left: -9999px;
      top: 0;
      width: ${contentWidth}px;
      font-family: ${cfg.fontFamily};
      font-size: ${cfg.fontSize}px;
      line-height: ${cfg.lineHeight};
      color: #000;
      background: #fff;
    `;
    container.innerHTML = html;
    document.body.appendChild(container);
    
    // 等待所有 base64 图片加载完成
    const images = container.querySelectorAll('img');
    console.log(`章节包含 ${images.length} 张图片`);
    
    for (const img of images) {
      const src = img.getAttribute('src') || '';
      console.log('图片 src:', src.substring(0, 50) + '...');
      
      if (src.startsWith('data:')) {
        // base64 图片，等待加载
        await new Promise<void>((resolve) => {
          if (img.complete && img.naturalWidth > 0) {
            resolve();
          } else {
            img.onload = () => resolve();
            img.onerror = () => {
              console.warn('图片加载失败');
              resolve();
            };
          }
        });
      }
      img.style.maxWidth = `${contentWidth}px`;
      img.style.height = 'auto';
      img.style.display = 'block';
    }
    
    // 收集所有可渲染元素（文本段落和图片）
    const elements: Element[] = [];
    
    // 递归收集元素
    const collectElements = (node: Element) => {
      // 如果是图片，直接添加
      if (node.tagName === 'IMG') {
        elements.push(node);
        return;
      }
      
      // 如果是文本块元素
      if (['P', 'H1', 'H2', 'H3', 'H4', 'H5', 'H6'].includes(node.tagName)) {
        // 检查是否包含图片
        const childImgs = node.querySelectorAll('img');
        if (childImgs.length > 0) {
          // 分别处理文本和图片
          childImgs.forEach(img => elements.push(img));
        }
        elements.push(node);
        return;
      }
      
      // 递归处理子元素
      for (const child of node.children) {
        collectElements(child);
      }
    };
    
    collectElements(container);
    
    // 如果没有收集到元素，尝试使用原始选择器
    if (elements.length === 0) {
      const fallback = container.querySelectorAll('p, h1, h2, h3, h4, h5, h6, div > img, img');
      fallback.forEach(el => elements.push(el));
    }
    
    console.log(`收集到 ${elements.length} 个可渲染元素`);
    
    const pages: Blob[] = [];
    let currentPageContent: Element[] = [];
    let currentHeight = 0;
    
    for (const el of elements) {
      const clone = el.cloneNode(true) as Element;
      
      // 测量元素高度
      const measureDiv = document.createElement('div');
      measureDiv.style.cssText = container.style.cssText;
      measureDiv.style.width = `${contentWidth}px`;
      measureDiv.appendChild(clone.cloneNode(true));
      document.body.appendChild(measureDiv);
      
      const height = measureDiv.offsetHeight;
      document.body.removeChild(measureDiv);
      
      if (currentHeight + height > contentHeight && currentPageContent.length > 0) {
        const pageBlob = await renderPageToBlob(currentPageContent, cfg);
        pages.push(pageBlob);
        currentPageContent = [];
        currentHeight = 0;
      }
      
      currentPageContent.push(clone);
      currentHeight += height;
    }
    
    if (currentPageContent.length > 0) {
      const pageBlob = await renderPageToBlob(currentPageContent, cfg);
      pages.push(pageBlob);
    }
    
    document.body.removeChild(container);
    
    if (pages.length === 0) {
      const canvas = document.createElement('canvas');
      canvas.width = SCREEN_WIDTH;
      canvas.height = SCREEN_HEIGHT;
      const ctx = canvas.getContext('2d')!;
      ctx.fillStyle = '#ffffff';
      ctx.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
      const blob = await new Promise<Blob>((resolve) => {
        canvas.toBlob((b) => resolve(b!), 'image/png');
      });
      pages.push(blob);
    }
    
    return pages;
  };

  // 生成封面
  const generateCover = async (title: string, author: string): Promise<Blob> => {
    const canvas = document.createElement('canvas');
    canvas.width = SCREEN_WIDTH;
    canvas.height = SCREEN_HEIGHT;
    const ctx = canvas.getContext('2d')!;
    
    const gradient = ctx.createLinearGradient(0, 0, 0, SCREEN_HEIGHT);
    gradient.addColorStop(0, '#f5f5f5');
    gradient.addColorStop(1, '#e0e0e0');
    ctx.fillStyle = gradient;
    ctx.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    
    ctx.strokeStyle = '#333333';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(60, 200);
    ctx.lineTo(SCREEN_WIDTH - 60, 200);
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(60, SCREEN_HEIGHT - 200);
    ctx.lineTo(SCREEN_WIDTH - 60, SCREEN_HEIGHT - 200);
    ctx.stroke();
    
    ctx.fillStyle = '#1a1a1a';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    
    const titleFontSize = Math.min(48, SCREEN_WIDTH / (title.length * 0.8));
    ctx.font = `bold ${titleFontSize}px serif`;
    
    const maxTitleWidth = SCREEN_WIDTH - 80;
    const titleLines = wrapText(ctx, title, maxTitleWidth);
    const titleStartY = SCREEN_HEIGHT / 2 - (titleLines.length * titleFontSize * 1.2) / 2;
    
    titleLines.forEach((line, i) => {
      ctx.fillText(line, SCREEN_WIDTH / 2, titleStartY + i * titleFontSize * 1.2);
    });
    
    if (author) {
      ctx.font = '24px serif';
      ctx.fillStyle = '#666666';
      ctx.fillText(author, SCREEN_WIDTH / 2, SCREEN_HEIGHT - 150);
    }
    
    return new Promise<Blob>((resolve) => {
      canvas.toBlob((blob) => resolve(blob!), 'image/png');
    });
  };

  // 第一步：开始转换（只渲染，不上传）
  const startConversion = async () => {
    if (!bookRef.current) return;
    
    abortRef.current = false;
    const book = bookRef.current;
    const bookId = generateBookId();
    
    try {
      setProgress({ status: 'converting', message: '正在生成封面...', current: 0, total: chapters.length + 1 });
      const coverBlob = await generateCover(bookTitle, bookAuthor);
      
      const sectionsData: Array<{ index: number; title: string; pages: Blob[] }> = [];
      const pagesList: Array<{ sectionIdx: number; pageIdx: number; blob: Blob }> = [];
      
      // 封面作为第一页预览
      pagesList.push({ sectionIdx: -1, pageIdx: 0, blob: coverBlob });
      
      for (let i = 0; i < chapters.length; i++) {
        if (abortRef.current) throw new Error('用户取消');
        
        const chapter = chapters[i];
        setProgress({
          status: 'converting',
          message: `正在渲染: ${chapter.title}`,
          current: i + 1,
          total: chapters.length + 1,
        });
        
        const html = await getChapterHtml(book, chapter.href);
        if (!html) {
          console.warn(`章节 ${chapter.title} 内容为空，跳过`);
          continue;
        }
        
        const pageBlobs = await renderHtmlToPages(html, config);
        const sectionIdx = sectionsData.length;
        
        sectionsData.push({
          index: sectionIdx,
          title: chapter.title,
          pages: pageBlobs,
        });
        
        pageBlobs.forEach((blob, pageIdx) => {
          pagesList.push({ sectionIdx, pageIdx, blob });
        });
      }
      
      const totalPages = sectionsData.reduce((sum, s) => sum + s.pages.length, 0);
      
      setConvertedBook({
        bookId,
        title: bookTitle,
        author: bookAuthor,
        cover: coverBlob,
        sections: sectionsData,
        totalPages,
      });
      
      setAllPages(pagesList);
      setPreviewIndex(0);
      
      setProgress({
        status: 'converted',
        message: `转换完成！共 ${sectionsData.length} 章，${totalPages} 页`,
        current: chapters.length + 1,
        total: chapters.length + 1,
      });
      
    } catch (error) {
      if ((error as Error).message === '用户取消') {
        setProgress({ status: 'idle', message: '已取消', current: 0, total: 0 });
      } else {
        console.error('转换失败:', error);
        setProgress({
          status: 'error',
          message: `转换失败: ${error instanceof Error ? error.message : '未知错误'}`,
          current: 0,
          total: 0,
        });
      }
    }
  };

  // 第二步：上传到设备
  const uploadToDevice = async () => {
    if (!convertedBook || !client) {
      alert('请先连接设备并完成转换');
      return;
    }
    
    try {
      setProgress({
        status: 'uploading',
        message: '正在上传到设备...',
        current: 0,
        total: 100,
      });
      
      await client.uploadBook(
        convertedBook.bookId,
        convertedBook.title,
        convertedBook.author || undefined,
        convertedBook.cover,
        convertedBook.sections,
        (message: string, progressPct: number) => {
          setProgress({
            status: 'uploading',
            message: message,
            current: progressPct,
            total: 100,
          });
        }
      );
      
      setProgress({
        status: 'completed',
        message: '上传完成！',
        current: 100,
        total: 100,
      });
      
      onComplete?.();
      
    } catch (error) {
      console.error('上传失败:', error);
      setProgress({
        status: 'error',
        message: `上传失败: ${error instanceof Error ? error.message : '未知错误'}`,
        current: 0,
        total: 0,
      });
    }
  };

  // 取消操作
  const handleCancel = () => {
    if (progress.status === 'converting' || progress.status === 'uploading') {
      abortRef.current = true;
    } else {
      onClose();
    }
  };

  // 配置更新
  const updateConfig = (key: keyof RenderConfig, value: number | string) => {
    setConfig(prev => ({ ...prev, [key]: value }));
    // 配置改变后清除转换结果
    setConvertedBook(null);
    setAllPages([]);
  };

  // 预览导航
  const prevPage = () => setPreviewIndex(i => Math.max(0, i - 1));
  const nextPage = () => setPreviewIndex(i => Math.min(allPages.length - 1, i + 1));

  const isProcessing = ['loading', 'converting', 'uploading'].includes(progress.status);
  const isConverted = progress.status === 'converted' || convertedBook !== null;
  const progressPercent = progress.total > 0 ? Math.round((progress.current / progress.total) * 100) : 0;

  // 当前预览图片
  const currentPreviewUrl = allPages[previewIndex] 
    ? URL.createObjectURL(allPages[previewIndex].blob) 
    : '';

  return (
    <div className="epub-to-images">
      <header className="converter-header">
        <div>
          <h2>📖 {bookTitle || '加载中...'}</h2>
          {bookAuthor && <p className="author">作者: {bookAuthor}</p>}
        </div>
        <button className="close-btn" onClick={handleCancel} disabled={progress.status === 'uploading'}>
          ✕
        </button>
      </header>

      <div className="converter-content">
        {/* 左侧：配置面板 */}
        <div className="config-panel">
          <h3>渲染设置</h3>
          
          <div className="config-group">
            <label>字体大小</label>
            <input
              type="range"
              min="20"
              max="40"
              value={config.fontSize}
              onChange={(e) => updateConfig('fontSize', Number(e.target.value))}
              disabled={isProcessing}
            />
            <span>{config.fontSize}px</span>
          </div>
          
          <div className="config-group">
            <label>行高</label>
            <input
              type="range"
              min="1.2"
              max="2.0"
              step="0.1"
              value={config.lineHeight}
              onChange={(e) => updateConfig('lineHeight', Number(e.target.value))}
              disabled={isProcessing}
            />
            <span>{config.lineHeight}</span>
          </div>
          
          <div className="config-group">
            <label>水平边距</label>
            <input
              type="range"
              min="10"
              max="50"
              value={config.paddingH}
              onChange={(e) => updateConfig('paddingH', Number(e.target.value))}
              disabled={isProcessing}
            />
            <span>{config.paddingH}px</span>
          </div>
          
          <div className="config-group">
            <label>垂直边距</label>
            <input
              type="range"
              min="10"
              max="50"
              value={config.paddingV}
              onChange={(e) => updateConfig('paddingV', Number(e.target.value))}
              disabled={isProcessing}
            />
            <span>{config.paddingV}px</span>
          </div>

          <div className="chapter-list">
            <h4>章节列表 ({chapters.length})</h4>
            <ul>
              {chapters.slice(0, 20).map((ch) => (
                <li key={ch.index} title={ch.title}>
                  {ch.index + 1}. {ch.title}
                </li>
              ))}
              {chapters.length > 20 && <li className="more">... 还有 {chapters.length - 20} 章</li>}
            </ul>
          </div>

          {/* 转换信息 */}
          {convertedBook && (
            <div className="convert-info">
              <h4>转换结果</h4>
              <p>📚 {convertedBook.sections.length} 章节</p>
              <p>📄 {convertedBook.totalPages} 页</p>
              <p className="book-id">ID: {convertedBook.bookId}</p>
            </div>
          )}
        </div>

        {/* 右侧：预览和进度 */}
        <div className="preview-panel">
          {allPages.length > 0 ? (
            <div className="preview-area">
              <div className="preview-container">
                <img src={currentPreviewUrl} alt="预览" className="preview-image" />
              </div>
              
              {/* 预览导航 */}
              <div className="preview-nav">
                <button onClick={prevPage} disabled={previewIndex === 0}>◀ 上一页</button>
                <span>{previewIndex + 1} / {allPages.length}</span>
                <button onClick={nextPage} disabled={previewIndex === allPages.length - 1}>下一页 ▶</button>
              </div>
            </div>
          ) : (
            <div className="preview-placeholder">
              <p>📖 预览区域</p>
              <p>点击"开始转换"后可预览渲染结果</p>
            </div>
          )}

          {/* 进度条 */}
          {isProcessing && (
            <div className="progress-container">
              <div className="progress-bar">
                <div className="progress-fill" style={{ width: `${progressPercent}%` }} />
              </div>
              <p className="progress-text">{progress.message}</p>
            </div>
          )}

          {/* 状态消息 */}
          {progress.status === 'error' && (
            <div className="error-message">❌ {progress.message}</div>
          )}
          {progress.status === 'completed' && (
            <div className="success-message">✅ {progress.message}</div>
          )}
        </div>
      </div>

      {/* 底部按钮 */}
      <footer className="converter-footer">
        <button className="secondary-btn" onClick={handleCancel}>
          {isProcessing ? '取消' : '关闭'}
        </button>
        
        {!isConverted ? (
          <button
            className="primary-btn"
            onClick={startConversion}
            disabled={isProcessing || chapters.length === 0}
          >
            {progress.status === 'converting' ? '转换中...' : '开始转换'}
          </button>
        ) : (
          <>
            <button
              className="secondary-btn"
              onClick={() => {
                setConvertedBook(null);
                setAllPages([]);
                setProgress({ status: 'idle', message: '', current: 0, total: 0 });
              }}
              disabled={isProcessing}
            >
              🔄 重新转换
            </button>
            <button
              className="primary-btn"
              onClick={uploadToDevice}
              disabled={isProcessing || !client}
            >
              {progress.status === 'uploading' ? '上传中...' : '📤 上传到设备'}
            </button>
          </>
        )}
      </footer>
    </div>
  );
}

export default EpubToImages;
