/**
 * EpubToImages - EPUB 电子书转换为 PNG 图片组件
 * 
 * 使用 CanvasRenderer 进行分页渲染，避免 OOM
 */

import { useEffect, useRef, useState, useCallback } from 'react';
import ePub, { Book, NavItem } from 'epubjs';
import { renderToPages, RenderConfig, PAGE_WIDTH, PAGE_HEIGHT, PageLinks } from './CanvasRenderer';
import type { IDeviceClient } from '../api/index';
import './EpubToImages.css';

interface EpubToImagesProps {
  file: File;
  onClose: () => void;
  onComplete?: () => void;
  client?: IDeviceClient;
}

const DEFAULT_CONFIG: RenderConfig = {
  fontFamily: 'Noto Sans SC, Microsoft YaHei, serif',
  fontSize: 24,
  fontWeight: 400,
  lineHeight: 1.6,
  paddingH: 20,
  paddingV: 15,
  textColor: '#000000',
  backgroundColor: '#ffffff',
  imageGrayscale: true,  // 永远启用灰度转换
  grayscaleLevels: 16,   // 默认16级灰度
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
    pageLinks?: PageLinks[];
  }>;
  totalPages: number;
  anchorMap?: Record<string, { section: number; page: number }>;
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
  
  const [convertedBook, setConvertedBook] = useState<ConvertedBook | null>(null);
  const [previewIndex, setPreviewIndex] = useState<number>(0);
  const [allPages, setAllPages] = useState<Array<{ sectionIdx: number; pageIdx: number; blob: Blob }>>([]);
  
  // 调试相关状态
  const [showDebugModal, setShowDebugModal] = useState<boolean>(false);
  const [debugHtml, setDebugHtml] = useState<string>('');
  const [debugChapterIndex, setDebugChapterIndex] = useState<number>(0);
  
  const bookRef = useRef<Book | null>(null);
  const abortRef = useRef<boolean>(false);

  // 生成纯英文数字的 bookId
  const generateBookId = (): string => {
    const timestamp = Date.now().toString(36);
    const random = Math.random().toString(36).substring(2, 8);
    return `book_${timestamp}_${random}`;
  };

  // Blob 转 DataURL
  const blobToDataUrl = (blob: Blob): Promise<string> => {
    return new Promise((resolve, reject) => {
      const reader = new FileReader();
      reader.onload = () => resolve(reader.result as string);
      reader.onerror = reject;
      reader.readAsDataURL(blob);
    });
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
        const toc = navigation.toc || [];
        const chapterList: ChapterInfo[] = [];
        
        // 从 spine 获取章节（更可靠）
        const spineItems = (book.spine as any).spineItems || [];
        console.log('Spine items:', spineItems.length);
        
        spineItems.forEach((item: any, idx: number) => {
          const tocItem = toc.find((t: NavItem) => item.href?.includes(t.href.split('#')[0]));
          chapterList.push({
            index: idx,
            title: tocItem?.label?.trim() || `章节 ${idx + 1}`,
            href: item.href,
          });
        });
        
        console.log('解析到的章节:', chapterList.length);
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

  // 处理 HTML 中的图片资源（将图片转换为 base64）
  const processHtmlWithResources = useCallback(async (html: string, sectionHref: string, book: Book) => {
    const archive = book.archive as any;
    let basePrefix = '';
    
    // 找到正确的基础路径
    if (archive && archive.zip) {
      const allFiles = Object.keys(archive.zip.files || {});
      const sectionFile = allFiles.find(f => f.endsWith(sectionHref) || f.endsWith('/' + sectionHref));
      if (sectionFile) {
        basePrefix = sectionFile.substring(0, sectionFile.length - sectionHref.length);
      }
    }
    
    const sectionDir = sectionHref.substring(0, sectionHref.lastIndexOf('/') + 1);
    
    // 解析相对路径
    const resolveImagePath = (src: string): string => {
      let resolved = '';
      if (src.startsWith('../')) {
        const parts = sectionDir.split('/').filter(p => p);
        const srcParts = src.split('/');
        let upCount = 0;
        for (const part of srcParts) {
          if (part === '..') upCount++;
          else break;
        }
        const baseParts = parts.slice(0, parts.length - upCount);
        const fileParts = srcParts.slice(upCount);
        resolved = [...baseParts, ...fileParts].join('/');
      } else if (!src.startsWith('/')) {
        resolved = sectionDir + src;
      } else {
        resolved = src;
      }
      return basePrefix + resolved;
    };

    // 从 archive 获取图片 blob
    const getImageBlob = async (imagePath: string): Promise<Blob | null> => {
      try {
        const blob = await book.archive.getBlob(imagePath);
        if (blob && blob.size > 0) return blob;
      } catch (e) {
        // 继续尝试方法2
      }
      
      const zipFile = archive?.zip?.files?.[imagePath];
      if (zipFile) {
        try {
          const uint8Array = await zipFile.async('uint8array');
          const mimeType = imagePath.endsWith('.png') ? 'image/png' : 'image/jpeg';
          return new Blob([uint8Array], { type: mimeType });
        } catch (e) {
          console.warn('zip 提取失败:', e);
        }
      }
      return null;
    };

    // 正则匹配所有图片
    const imgRegex = /<img[^>]+src=["']([^"']+)["'][^>]*>/gi;
    const svgImageRegex = /<image[^>]+(?:xlink:)?href=["']([^"']+)["'][^>]*\/?>/gi;
    
    let processedHtml = html;
    
    // 处理 img 标签
    const imgMatches = [...html.matchAll(imgRegex)];
    for (const match of imgMatches) {
      const fullTag = match[0];
      const src = match[1];
      
      if (src && !src.startsWith('data:') && !src.startsWith('http')) {
        try {
          const imagePath = resolveImagePath(src);
          const blob = await getImageBlob(imagePath);
          if (blob) {
            const dataUrl = await blobToDataUrl(blob);
            const newTag = fullTag.replace(src, dataUrl);
            processedHtml = processedHtml.replace(fullTag, newTag);
            console.log('图片转换成功:', src);
          }
        } catch (e) {
          console.warn('无法加载图片:', src, e);
        }
      }
    }
    
    // 处理 SVG image 标签
    const svgMatches = [...html.matchAll(svgImageRegex)];
    for (const match of svgMatches) {
      const fullTag = match[0];
      const src = match[1];
      
      if (src && !src.startsWith('data:') && !src.startsWith('http')) {
        try {
          const imagePath = resolveImagePath(src);
          const blob = await getImageBlob(imagePath);
          if (blob) {
            const dataUrl = await blobToDataUrl(blob);
            const newTag = fullTag.replace(src, dataUrl);
            processedHtml = processedHtml.replace(fullTag, newTag);
          }
        } catch (e) {
          console.warn('无法加载 SVG image:', src, e);
        }
      }
    }

    return processedHtml;
  }, []);

  // 从 EPUB 提取封面图片
  const extractCoverFromEpub = async (book: Book): Promise<Blob | null> => {
    try {
      // 尝试获取封面 URL
      const coverUrl = await book.coverUrl();
      if (!coverUrl) {
        console.log('EPUB 没有封面元数据，尝试从文件中查找');
        return null;
      }
      
      // 加载封面图片
      const response = await fetch(coverUrl);
      const blob = await response.blob();
      
      // 如果是 JPEG/PNG，转换为适合 E-Ink 的灰度图
      return await convertImageForEink(blob);
    } catch (error) {
      console.error('提取封面失败:', error);
      return null;
    }
  };

  // 将图片转换为适合 E-Ink 的格式（灰度、压缩）
  const convertImageForEink = async (imageBlob: Blob): Promise<Blob> => {
    return new Promise((resolve) => {
      const img = new Image();
      img.onload = () => {
        const COVER_SIZE = 160;
        
        // 1. 计算缩放比例：让短边等于 160
        const scale = COVER_SIZE / Math.min(img.width, img.height);
        const scaledWidth = Math.round(img.width * scale);
        const scaledHeight = Math.round(img.height * scale);
        
        // 2. 创建临时 canvas 进行缩放
        const tempCanvas = document.createElement('canvas');
        tempCanvas.width = scaledWidth;
        tempCanvas.height = scaledHeight;
        const tempCtx = tempCanvas.getContext('2d')!;
        tempCtx.drawImage(img, 0, 0, scaledWidth, scaledHeight);
        
        // 3. 创建最终 160x160 canvas，从左上角裁剪
        const canvas = document.createElement('canvas');
        canvas.width = COVER_SIZE;
        canvas.height = COVER_SIZE;
        const ctx = canvas.getContext('2d')!;
        
        // 从缩放后的图片左上角裁剪 160x160
        ctx.drawImage(tempCanvas, 0, 0, COVER_SIZE, COVER_SIZE, 0, 0, COVER_SIZE, COVER_SIZE);
        const imageData = ctx.getImageData(0, 0, COVER_SIZE, COVER_SIZE);
        const data = imageData.data;
        
        // 4. 16 级灰度转换
        for (let i = 0; i < data.length; i += 4) {
          const gray = Math.round(0.299 * data[i] + 0.587 * data[i + 1] + 0.114 * data[i + 2]);
          const level = Math.round(gray / 255 * 15);
          const finalGray = Math.round(level / 15 * 255);
          data[i] = data[i + 1] = data[i + 2] = finalGray;
        }
        
        ctx.putImageData(imageData, 0, 0);
        canvas.toBlob((blob) => resolve(blob!), 'image/png', 1.0);
      };
      img.src = URL.createObjectURL(imageBlob);
    });
  };

  // 生成封面
  const generateCover = async (title: string, author: string): Promise<Blob> => {
    const canvas = document.createElement('canvas');
    canvas.width = PAGE_WIDTH;
    canvas.height = PAGE_HEIGHT;
    const ctx = canvas.getContext('2d')!;
    
    ctx.fillStyle = '#f5f5f5';
    ctx.fillRect(0, 0, PAGE_WIDTH, PAGE_HEIGHT);
    
    // 装饰线
    ctx.strokeStyle = '#333333';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(60, 150);
    ctx.lineTo(PAGE_WIDTH - 60, 150);
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(60, PAGE_HEIGHT - 150);
    ctx.lineTo(PAGE_WIDTH - 60, PAGE_HEIGHT - 150);
    ctx.stroke();
    
    // 标题
    ctx.fillStyle = '#1a1a1a';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    
    const titleFontSize = Math.min(48, PAGE_WIDTH / (title.length * 0.8));
    ctx.font = `bold ${titleFontSize}px serif`;
    
    // 换行处理
    const maxWidth = PAGE_WIDTH - 80;
    const words = title.split('');
    const lines: string[] = [];
    let currentLine = '';
    
    for (const char of words) {
      const testLine = currentLine + char;
      if (ctx.measureText(testLine).width > maxWidth && currentLine) {
        lines.push(currentLine);
        currentLine = char;
      } else {
        currentLine = testLine;
      }
    }
    if (currentLine) lines.push(currentLine);
    
    const lineHeight = titleFontSize * 1.2;
    const startY = PAGE_HEIGHT / 2 - (lines.length * lineHeight) / 2;
    lines.forEach((line, i) => {
      ctx.fillText(line, PAGE_WIDTH / 2, startY + i * lineHeight);
    });
    
    // 作者
    if (author) {
      ctx.font = '24px serif';
      ctx.fillStyle = '#666666';
      ctx.fillText(author, PAGE_WIDTH / 2, PAGE_HEIGHT - 100);
    }
    
    return new Promise<Blob>((resolve) => {
      canvas.toBlob((blob) => resolve(blob!), 'image/png');
    });
  };

  // 开始转换
  const startConversion = async () => {
    if (!bookRef.current) return;
    
    abortRef.current = false;
    const book = bookRef.current;
    const bookId = generateBookId();
    
    try {
      // 1. 提取真实封面（优先使用 EPUB 封面）
      setProgress({ status: 'converting', message: '正在提取封面...', current: 0, total: chapters.length + 1 });
      let coverBlob = await extractCoverFromEpub(book);
      if (!coverBlob) {
        console.log('EPUB 没有封面，生成默认封面');
        coverBlob = await generateCover(bookTitle, bookAuthor);
      }
      
      const sectionsData: Array<{ 
        index: number; 
        title: string; 
        pages: Blob[];
        pageLinks: PageLinks[];
      }> = [];
      const pagesList: Array<{ sectionIdx: number; pageIdx: number; blob: Blob }> = [];
      const globalAnchorMap = new Map<string, { section: number; page: number }>();
      
      // 注意：封面不放入 pagesList，因为它不是阅读页面
      
      for (let i = 0; i < chapters.length; i++) {
        if (abortRef.current) throw new Error('用户取消');
        
        const chapter = chapters[i];
        setProgress({
          status: 'converting',
          message: `正在渲染: ${chapter.title} (${i + 1}/${chapters.length})`,
          current: i + 1,
          total: chapters.length + 1,
        });
        
        try {
          // 获取章节内容
          const section = book.spine.get(chapter.index);
          if (!section) {
            console.warn(`章节 ${i} 不存在`);
            continue;
          }
          
          const contents = await section.load((book as any).load.bind(book));
          const serializer = new XMLSerializer();
          let html = serializer.serializeToString(contents);
          
          // 移除 xmlns
          if (html.includes('xmlns')) {
            html = html.replace(/xmlns="[^"]*"/g, '');
          }
          
          // 处理图片资源（转换为 base64）
          const processedHtml = await processHtmlWithResources(html, section.href, book);
          
          // 检测是否有图片（检测 <img> 标签和 SVG <image> 标签）
          const hasImages = /<img[^>]+src=/i.test(processedHtml) || 
                           /<image[^>]+(href|xlink:href)=/i.test(processedHtml);
          
          // 动态调整灰度级别：有图片用16级，纯文本用2级（激进二值图）
          const chapterConfig = {
            ...config,
            grayscaleLevels: hasImages ? 16 : 2,
          };
          
          // 使用 CanvasRenderer 分页渲染（避免 OOM）
          const renderResult = await renderToPages(processedHtml, chapterConfig, (msg) => {
            setProgress(prev => ({ ...prev, message: `${chapter.title}: ${msg}` }));
          });
          
          console.log(`章节 ${i + 1}: ${chapter.title}, ${renderResult.pages.length} 页, ${renderResult.pageLinks.length} 个链接页`);
          
          const sectionIdx = sectionsData.length;
          const currentSection = sectionIdx + 1; // 章节索引从 1 开始
          
          // 处理章节内的锚点，添加到全局锚点映射
          renderResult.anchors.forEach((anchor, key) => {
            globalAnchorMap.set(key, {
              section: currentSection,
              page: anchor.page
            });
          });
          
          sectionsData.push({
            index: sectionIdx,
            title: chapter.title,
            pages: renderResult.pages,
            pageLinks: renderResult.pageLinks,
          });
          
          renderResult.pages.forEach((blob, pageIdx) => {
            pagesList.push({ sectionIdx, pageIdx, blob });
          });
          
        } catch (e) {
          console.error(`章节 ${i} 转换失败:`, e);
        }
      }
      
      const totalPages = sectionsData.reduce((sum, s) => sum + s.pages.length, 0);
      
      // 转换锚点 Map 为普通对象
      const anchorMapObj = Object.fromEntries(globalAnchorMap);
      
      setConvertedBook({
        bookId,
        title: bookTitle,
        author: bookAuthor,
        cover: coverBlob,
        sections: sectionsData,
        totalPages,
        anchorMap: anchorMapObj,
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

  // 上传到设备
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
        convertedBook.anchorMap,
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

  const handleCancel = () => {
    if (progress.status === 'converting' || progress.status === 'uploading') {
      abortRef.current = true;
    } else {
      onClose();
    }
  };

  const updateConfig = (key: keyof RenderConfig, value: number | string | boolean) => {
    setConfig(prev => ({ ...prev, [key]: value }));
    setConvertedBook(null);
    setAllPages([]);
  };

  const prevPage = () => setPreviewIndex(i => Math.max(0, i - 1));
  const nextPage = () => setPreviewIndex(i => Math.min(allPages.length - 1, i + 1));

  // 调试：查看章节原始 HTML
  const viewChapterHtml = async () => {
    if (!bookRef.current || chapters.length === 0) {
      alert('请先加载 EPUB 文件');
      return;
    }
    
    try {
      const book = bookRef.current;
      const chapter = chapters[debugChapterIndex];
      
      const section = book.spine.get(chapter.index);
      if (!section) {
        alert(`章节 ${debugChapterIndex} 不存在`);
        return;
      }
      
      const contents = await section.load((book as any).load.bind(book));
      const serializer = new XMLSerializer();
      let html = serializer.serializeToString(contents);
      
      // 移除 xmlns
      if (html.includes('xmlns')) {
        html = html.replace(/xmlns="[^"]*"/g, '');
      }
      
      setDebugHtml(html);
      setShowDebugModal(true);
    } catch (error) {
      console.error('获取章节 HTML 失败:', error);
      alert(`错误: ${error instanceof Error ? error.message : '未知错误'}`);
    }
  };
  
  const copyHtmlToClipboard = () => {
    navigator.clipboard.writeText(debugHtml).then(() => {
      alert('HTML 已复制到剪贴板');
    }).catch(err => {
      console.error('复制失败:', err);
    });
  };

  const isProcessing = ['loading', 'converting', 'uploading'].includes(progress.status);
  const isConverted = progress.status === 'converted' || convertedBook !== null;
  const progressPercent = progress.total > 0 ? Math.round((progress.current / progress.total) * 100) : 0;

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
        <div className="config-panel">
          <h3>渲染设置</h3>
          
          <div className="config-group">
            <label>字体大小</label>
            <input
              type="range"
              min="16"
              max="32"
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
              max="2.5"
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

          <div className="config-group">
            <label>灰度优化</label>
            <div style={{ fontSize: '12px', color: '#666', marginTop: '4px' }}>
              ✓ 自动灰度转换<br/>
              ✓ 纯文本: 二值图 (2级)<br/>
              ✓ 含图片: 16级灰度
            </div>
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

          {/* 调试工具 */}
          {chapters.length > 0 && (
            <div className="debug-panel" style={{ marginTop: '20px', padding: '12px', backgroundColor: '#f8f9fa', borderRadius: '4px' }}>
              <h4 style={{ margin: '0 0 10px 0', fontSize: '14px', color: '#333' }}>🔍 调试工具</h4>
              <div style={{ display: 'flex', gap: '8px', alignItems: 'center', flexWrap: 'wrap' }}>
                <select 
                  value={debugChapterIndex} 
                  onChange={(e) => setDebugChapterIndex(Number(e.target.value))}
                  style={{ padding: '4px 8px', borderRadius: '3px', border: '1px solid #ddd' }}
                >
                  {chapters.map((ch, idx) => (
                    <option key={idx} value={idx}>
                      章节 {idx + 1}: {ch.title.substring(0, 30)}
                    </option>
                  ))}
                </select>
                <button 
                  onClick={viewChapterHtml}
                  disabled={isProcessing}
                  style={{ padding: '4px 12px', fontSize: '13px' }}
                >
                  查看原始 HTML
                </button>
              </div>
              <div style={{ fontSize: '11px', color: '#666', marginTop: '6px' }}>
                可以查看 EPUB 章节的原始 HTML 代码
              </div>
            </div>
          )}

          {convertedBook && (
            <div className="convert-info">
              <h4>转换结果</h4>
              <p>📚 {convertedBook.sections.length} 章节</p>
              <p>📄 {convertedBook.totalPages} 页</p>
              <p className="book-id">ID: {convertedBook.bookId}</p>
            </div>
          )}
        </div>

        <div className="preview-panel">
          {allPages.length > 0 ? (
            <div className="preview-area">
              <div className="preview-container">
                <img src={currentPreviewUrl} alt="预览" className="preview-image" />
              </div>
              
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

          {isProcessing && (
            <div className="progress-container">
              <div className="progress-bar">
                <div className="progress-fill" style={{ width: `${progressPercent}%` }} />
              </div>
              <p className="progress-text">{progress.message}</p>
            </div>
          )}

          {progress.status === 'error' && (
            <div className="error-message">❌ {progress.message}</div>
          )}
          {progress.status === 'completed' && (
            <div className="success-message">✅ {progress.message}</div>
          )}
        </div>
      </div>

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

      {/* 调试模态框 */}
      {showDebugModal && (
        <div className="modal-overlay" onClick={() => setShowDebugModal(false)}>
          <div className="modal-content" onClick={(e) => e.stopPropagation()} style={{ maxWidth: '900px', maxHeight: '80vh' }}>
            <div className="modal-header">
              <h3>原始 HTML - 章节 {debugChapterIndex + 1}</h3>
              <button className="close-btn" onClick={() => setShowDebugModal(false)}>×</button>
            </div>
            <div className="modal-body" style={{ maxHeight: 'calc(80vh - 120px)', overflow: 'auto' }}>
              <div style={{ marginBottom: '10px', display: 'flex', gap: '8px' }}>
                <button onClick={copyHtmlToClipboard} style={{ padding: '6px 12px', fontSize: '13px' }}>
                  📋 复制到剪贴板
                </button>
                <span style={{ fontSize: '12px', color: '#666', lineHeight: '32px' }}>
                  共 {debugHtml.length} 字符
                </span>
              </div>
              <pre style={{
                backgroundColor: '#f5f5f5',
                padding: '16px',
                borderRadius: '4px',
                fontSize: '12px',
                lineHeight: '1.5',
                overflow: 'auto',
                whiteSpace: 'pre-wrap',
                wordBreak: 'break-all'
              }}>
                {debugHtml}
              </pre>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

export default EpubToImages;
