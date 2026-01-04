import { useEffect, useRef, useState, useCallback } from 'react';
import ePub, { Book, NavItem } from 'epubjs';
import html2canvas from 'html2canvas';
import { ProcessorManager } from './processors';
import { ConfigRenderer } from './ConfigRenderer';
import './EpubToImages.css';

// M5PaperS3 屏幕尺寸
const SCREEN_WIDTH = 540;

interface EpubToImagesProps {
  file: File;
  onClose: () => void;
}

interface ChapterImage {
  index: number;
  title: string;
  dataUrl: string;
  width: number;
  height: number;
}

// 渲染配置
interface RenderConfig {
  fontFamily: string;
  fontSize: number;
  fontWeight: number;  // 字体粗度 100-900
  lineHeight: number;
  paddingH: number;  // 水平边距
  paddingV: number;  // 垂直边距
  textColor: string;
  backgroundColor: string;
  imageGrayscale: boolean;  // 输出灰度图片
  grayscaleLevels: number;  // 灰度级数 (16 for M5PaperS3)
}

const defaultConfig: RenderConfig = {
  fontFamily: 'Noto Sans SC, Microsoft YaHei, sans-serif',
  fontSize: 24,
  fontWeight: 600,
  lineHeight: 1.6,
  paddingH: 20,
  paddingV: 15,
  textColor: '#000000',
  backgroundColor: '#ffffff',
  imageGrayscale: true,
  grayscaleLevels: 16,
};

// localStorage 配置键
const CONFIG_STORAGE_KEY = 'epub-render-config';

// 从 localStorage 加载配置
const loadConfig = (): RenderConfig => {
  try {
    const saved = localStorage.getItem(CONFIG_STORAGE_KEY);
    if (saved) {
      return { ...defaultConfig, ...JSON.parse(saved) };
    }
  } catch (e) {
    console.warn('加载配置失败:', e);
  }
  return defaultConfig;
};

// 保存配置到 localStorage
const saveConfig = (config: RenderConfig) => {
  try {
    localStorage.setItem(CONFIG_STORAGE_KEY, JSON.stringify(config));
  } catch (e) {
    console.warn('保存配置失败:', e);
  }
};

const fontOptions = [
  { value: 'Noto Sans SC, Microsoft YaHei, sans-serif', label: '思源黑体 / 微软雅黑' },
  { value: 'Noto Serif SC, SimSun, serif', label: '思源宋体 / 宋体' },
  { value: 'LXGW WenKai, cursive', label: '霞鹜文楷' },
  { value: 'Source Han Sans SC, sans-serif', label: '思源黑体' },
  { value: 'PingFang SC, sans-serif', label: '苹方' },
];

const EpubToImages = ({ file, onClose }: EpubToImagesProps) => {
  const renderRef = useRef<HTMLDivElement>(null);
  const [book, setBook] = useState<Book | null>(null);
  const [bookInfo, setBookInfo] = useState<any>(null);
  const [toc, setToc] = useState<NavItem[]>([]);
  const [spineItems, setSpineItems] = useState<any[]>([]);
  const [currentSpineIndex, setCurrentSpineIndex] = useState<number>(0);
  const [currentHtml, setCurrentHtml] = useState<string>('');
  
  // 配置相关
  const [config, setConfig] = useState<RenderConfig>(loadConfig);
  
  // 截图相关状态
  const [isConverting, setIsConverting] = useState(false);
  const [convertProgress, setConvertProgress] = useState<string>('');
  const [chapterImages, setChapterImages] = useState<ChapterImage[]>([]);

  // 处理器管理
  const [processorManager] = useState(() => {
    const pm = new ProcessorManager();
    // 加载保存的处理器配置
    pm.loadFromLocalStorage();
    return pm;
  });
  const [, forceUpdate] = useState({});

  // 保存处理器配置
  const saveProcessorConfig = () => {
    processorManager.saveToLocalStorage();
  };

  // 更新配置
  const updateConfig = (key: keyof RenderConfig, value: any) => {
    setConfig(prev => {
      const newConfig = { ...prev, [key]: value };
      saveConfig(newConfig);
      return newConfig;
    });
  };

  // 导出配置
  const exportConfig = () => {
    const dataStr = JSON.stringify(config, null, 2);
    const blob = new Blob([dataStr], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = 'epub-render-config.json';
    link.click();
    URL.revokeObjectURL(url);
  };

  // 导入配置
  const importConfig = () => {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.json';
    input.onchange = async (e) => {
      const file = (e.target as HTMLInputElement).files?.[0];
      if (file) {
        try {
          const text = await file.text();
          const imported = JSON.parse(text);
          const newConfig = { ...defaultConfig, ...imported };
          setConfig(newConfig);
          saveConfig(newConfig);
          alert('配置导入成功！');
        } catch (err) {
          alert('配置导入失败：' + err);
        }
      }
    };
    input.click();
  };
  // ============ 预设管理 (Preset Management) ============
  
  // 导出预设（渲染配置 + 处理器配置）
  const exportPreset = () => {
    const preset = {
      version: '1.0',
      exportTime: new Date().toISOString(),
      renderConfig: config,
      processors: processorManager.exportConfig(),
    };
    
    const dataStr = JSON.stringify(preset, null, 2);
    const blob = new Blob([dataStr], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = `epub-preset-${new Date().toISOString().split('T')[0]}.json`;
    link.click();
    URL.revokeObjectURL(url);
  };

  // 导入预设
  const importPreset = () => {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.json';
    input.onchange = async (e) => {
      const file = (e.target as HTMLInputElement).files?.[0];
      if (file) {
        try {
          const text = await file.text();
          const preset = JSON.parse(text);
          
          let messages = [];
          
          // 导入渲染配置
          if (preset.renderConfig) {
            const newConfig = { ...defaultConfig, ...preset.renderConfig };
            setConfig(newConfig);
            saveConfig(newConfig);
            messages.push('✅ 渲染配置导入成功');
          }
          
          // 导入处理器配置
          if (preset.processors) {
            const results = processorManager.importConfig(preset.processors);
            saveProcessorConfig();
            
            if (results.success.length > 0) {
              messages.push('✅ 处理器: ' + results.success.join(', '));
            }
            if (results.warnings.length > 0) {
              messages.push('⚠️ ' + results.warnings.join(', '));
            }
            if (results.errors.length > 0) {
              messages.push('❌ ' + results.errors.join(', '));
            }
          }
          
          alert('预设导入完成！\n\n' + messages.join('\n'));
          forceUpdate({});
        } catch (err) {
          alert('预设导入失败：' + err);
        }
      }
    };
    input.click();
  };

  // 重置预设（所有配置）
  const resetPreset = () => {
    if (confirm('确定要重置所有配置（渲染设置 + 文本处理）吗？')) {
      // 重置渲染配置
      setConfig(defaultConfig);
      saveConfig(defaultConfig);
      
      // 重置处理器配置
      processorManager.getProcessors().forEach(p => {
        p.enabled = false;
        if (p.setConfig && p.configItems) {
          const defaultConfig: any = {};
          p.configItems.forEach(item => {
            defaultConfig[item.key] = item.defaultValue;
          });
          p.setConfig(defaultConfig);
        }
      });
      saveProcessorConfig();
      
      alert('✅ 所有配置已重置');
      forceUpdate({});
    }
  };

  // 加载书籍
  useEffect(() => {
    if (!file) return;

    const loadBook = async () => {
      try {
        const arrayBuffer = await file.arrayBuffer();
        const bookInstance = ePub(arrayBuffer);
        setBook(bookInstance);

        await bookInstance.ready;
        
        const metadata = await bookInstance.loaded.metadata;
        const navigation = await bookInstance.loaded.navigation;
        
        // 获取 spine items
        const items = (bookInstance.spine as any).spineItems || [];
        setSpineItems(items);
        
        setBookInfo({
          title: metadata.title,
          author: metadata.creator,
          spineLength: items.length,
          tocLength: navigation.toc.length,
        });

        setToc(navigation.toc);
        console.log('书籍加载完成，章节数:', items.length);

        // 显示第一章
        if (items.length > 0) {
          await displaySection(bookInstance, 0);
        }

      } catch (error) {
        console.error('加载 EPUB 失败:', error);
        alert('加载 EPUB 失败: ' + (error as Error).message);
      }
    };

    loadBook();
  }, [file]);

  // 渲染章节内容 (真正的 renderless 模式)
  const displaySection = async (bookInstance: Book, index: number) => {
    const items = (bookInstance.spine as any).spineItems || [];
    if (index < 0 || index >= items.length) return;

    const section = bookInstance.spine.get(index);
    if (section) {
      try {
        // 使用 section.load() 获取文档，然后序列化
        const contents = await section.load((bookInstance as any).load.bind(bookInstance));
        
        // 序列化为 HTML 字符串
        const serializer = new XMLSerializer();
        let html = serializer.serializeToString(contents);
        
        // 如果是 XHTML，转换为 HTML
        if (html.includes('xmlns')) {
          html = html.replace(/xmlns="[^"]*"/g, '');
        }
        
        // 处理图片资源
        let processedHtml = await processHtmlWithResources(html, section.href, bookInstance);
        
        // 应用文本处理器
        processedHtml = await processorManager.processHtml({
          html: processedHtml,
          config,
          sectionIndex: index,
          totalSections: items.length,
        });
        
        setCurrentHtml(processedHtml);
        setCurrentSpineIndex(index);
        console.log(`渲染章节 ${index}:`, section.href);
      } catch (e) {
        console.error('渲染章节失败:', e);
      }
    }
  };

  // 处理图片 URL（EPUB 内的相对路径）
  const processHtmlWithResources = useCallback(async (html: string, sectionHref: string, bookInstance: Book) => {
    // 列出 archive 中的所有文件，找到正确的基础路径
    const archive = bookInstance.archive as any;
    let basePrefix = '';
    
    if (archive && archive.zip) {
      const allFiles = Object.keys(archive.zip.files || {});
      
      // 找到包含 sectionHref 的完整路径，确定前缀
      const sectionFile = allFiles.find(f => f.endsWith(sectionHref) || f.endsWith('/' + sectionHref));
      if (sectionFile) {
        basePrefix = sectionFile.substring(0, sectionFile.length - sectionHref.length);
      }
    }
    
    // 获取 section 的基础路径
    const sectionDir = sectionHref.substring(0, sectionHref.lastIndexOf('/') + 1);
    
    // 用于解析相对路径的辅助函数
    const resolveImagePath = (src: string): string => {
      let resolved = '';
      if (src.startsWith('../')) {
        // 处理 ../Images/xxx.jpg 这种路径
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
      // 添加基础前缀
      return basePrefix + resolved;
    };

    // 使用正则表达式找到所有图片
    const imgRegex = /<img[^>]+src=["']([^"']+)["'][^>]*>/gi;
    const svgImageRegex = /<image[^>]+(?:xlink:)?href=["']([^"']+)["'][^>]*\/?>/gi;
    
    const imgMatches = [...html.matchAll(imgRegex)];
    const svgMatches = [...html.matchAll(svgImageRegex)];
    
    let processedHtml = html;
    
    // 辅助函数：从 archive 获取图片 blob
    const getImageBlob = async (imagePath: string): Promise<Blob | null> => {
      try {
        const blob = await bookInstance.archive.getBlob(imagePath);
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
    
    // 处理普通 img 标签
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
          }
        } catch (e) {
          console.warn('无法加载 img 图片:', src, e);
        }
      }
    }
    
    // 处理 SVG image 标签
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

  const blobToDataUrl = (blob: Blob): Promise<string> => {
    return new Promise((resolve, reject) => {
      const reader = new FileReader();
      reader.onload = () => resolve(reader.result as string);
      reader.onerror = reject;
      reader.readAsDataURL(blob);
    });
  };

  // 将图片转换为指定灰度级数
  const convertToGrayscale = (dataUrl: string, levels: number): Promise<string> => {
    return new Promise((resolve) => {
      const img = new Image();
      img.onload = () => {
        const canvas = document.createElement('canvas');
        canvas.width = img.width;
        canvas.height = img.height;
        const ctx = canvas.getContext('2d')!;
        
        // 绘制原图
        ctx.drawImage(img, 0, 0);
        
        // 获取像素数据
        const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
        const data = imageData.data;
        
        // 转换为灰度并量化到指定级数
        const step = 256 / levels;
        for (let i = 0; i < data.length; i += 4) {
          // 计算灰度值 (使用标准亮度公式)
          const gray = 0.299 * data[i] + 0.587 * data[i + 1] + 0.114 * data[i + 2];
          // 量化到指定级数
          const quantized = Math.round(gray / step) * step;
          const finalGray = Math.min(255, quantized);
          
          data[i] = finalGray;     // R
          data[i + 1] = finalGray; // G
          data[i + 2] = finalGray; // B
        }
        
        ctx.putImageData(imageData, 0, 0);
        resolve(canvas.toDataURL('image/png'));
      };
      img.src = dataUrl;
    });
  };

  // 应用自定义样式到 HTML
  const applyCustomStyles = (html: string): string => {
    const maxImageWidth = SCREEN_WIDTH - config.paddingH * 2;
    const styleOverride = `
      <style>
        body, html {
          margin: 0 !important;
          padding: 0 !important;
          background: ${config.backgroundColor} !important;
        }
        body {
          font-family: ${config.fontFamily} !important;
          font-size: ${config.fontSize}px !important;
          font-weight: ${config.fontWeight} !important;
          line-height: ${config.lineHeight} !important;
          color: ${config.textColor} !important;
          padding: ${config.paddingV}px ${config.paddingH}px !important;
          box-sizing: border-box !important;
          width: ${SCREEN_WIDTH}px !important;
          overflow-x: hidden !important;
        }
        p, div, span, li, td, th {
          font-family: inherit !important;
          font-size: inherit !important;
          font-weight: inherit !important;
          line-height: inherit !important;
          color: inherit !important;
        }
        h1, h2, h3, h4, h5, h6 {
          font-family: inherit !important;
          color: inherit !important;
          margin-top: 1em !important;
          margin-bottom: 0.5em !important;
        }
        h1 { font-size: 1.5em !important; font-weight: bold !important; }
        h2 { font-size: 1.3em !important; font-weight: bold !important; }
        h3 { font-size: 1.2em !important; font-weight: bold !important; }
        img {
          max-width: ${maxImageWidth}px !important;
          width: auto !important;
          height: auto !important;
          display: block !important;
        }
        svg {
          max-width: ${maxImageWidth}px !important;
          width: 100% !important;
          height: auto !important;
          display: block !important;
        }
        svg image {
          max-width: 100% !important;
        }
        * {
          background-color: transparent !important;
          max-width: ${SCREEN_WIDTH}px !important;
          box-sizing: border-box !important;
        }
        body {
          background-color: ${config.backgroundColor} !important;
          max-width: ${SCREEN_WIDTH}px !important;
        }
      </style>
    `;
    
    if (html.includes('</head>')) {
      return html.replace('</head>', styleOverride + '</head>');
    } else {
      return styleOverride + html;
    }
  };

  // 使用 iframe 进行隔离渲染和截图
  const captureHtmlToImage = async (html: string): Promise<{ dataUrl: string; height: number }> => {
    return new Promise((resolve, reject) => {
      // 创建一个隐藏的 iframe
      const iframe = document.createElement('iframe');
      iframe.style.cssText = `
        position: fixed;
        left: -9999px;
        top: 0;
        width: ${SCREEN_WIDTH}px;
        border: none;
        background: ${config.backgroundColor};
      `;
      document.body.appendChild(iframe);

      const iframeDoc = iframe.contentDocument || iframe.contentWindow?.document;
      if (!iframeDoc) {
        document.body.removeChild(iframe);
        reject(new Error('无法访问 iframe 文档'));
        return;
      }

      // 写入 HTML
      iframeDoc.open();
      iframeDoc.write(html);
      iframeDoc.close();

      // 等待内容加载
      const checkReady = async () => {
        await new Promise(r => setTimeout(r, 300));
        
        // 等待图片加载
        const images = iframeDoc.querySelectorAll('img');
        await Promise.all(Array.from(images).map(img => {
          if (img.complete) return Promise.resolve();
          return new Promise<void>(r => {
            img.onload = () => r();
            img.onerror = () => r();
          });
        }));

        await new Promise(r => setTimeout(r, 100));

        // 获取内容高度
        const body = iframeDoc.body;
        const height = Math.max(body.scrollHeight, body.offsetHeight, 100);
        
        // 调整 iframe 高度
        iframe.style.height = `${height}px`;
        
        await new Promise(r => setTimeout(r, 100));

        try {
          // 使用 html2canvas 截图
          const canvas = await html2canvas(body, {
            width: SCREEN_WIDTH,
            height: height,
            backgroundColor: config.backgroundColor,
            scale: 1,
            useCORS: true,
            allowTaint: true,
            logging: false,
          });
          
          const dataUrl = canvas.toDataURL('image/png');
          
          document.body.removeChild(iframe);
          resolve({ dataUrl, height });
        } catch (e) {
          document.body.removeChild(iframe);
          reject(e);
        }
      };

      checkReady();
    });
  };

  // 开始转换 - 每章节一张图片
  const startConversion = useCallback(async () => {
    if (!book) return;

    setIsConverting(true);
    setChapterImages([]);

    const items = (book.spine as any).spineItems || [];
    const images: ChapterImage[] = [];

    for (let i = 0; i < items.length; i++) {
      setConvertProgress(`处理章节 ${i + 1}/${items.length}...`);
      
      try {
        const section = book.spine.get(i);
        if (!section) continue;

        const contents = await section.load((book as any).load.bind(book));
        const serializer = new XMLSerializer();
        let html = serializer.serializeToString(contents);
        if (html.includes('xmlns')) {
          html = html.replace(/xmlns="[^"]*"/g, '');
        }
        
        let processedHtml = await processHtmlWithResources(html, section.href, book);
        
        // 应用文本处理器
        processedHtml = await processorManager.processHtml({
          html: processedHtml,
          config,
          sectionIndex: i,
          totalSections: items.length,
        });
        
        const styledHtml = applyCustomStyles(processedHtml);
        
        // 使用 iframe 截图
        const { dataUrl, height } = await captureHtmlToImage(styledHtml);

        let finalDataUrl = dataUrl;
        if (config.imageGrayscale) {
          finalDataUrl = await convertToGrayscale(dataUrl, config.grayscaleLevels);
        }

        const tocItem = toc.find(t => section.href.includes(t.href.split('#')[0]));
        const title = tocItem?.label || `Chapter ${i + 1}`;

        images.push({
          index: i,
          title,
          dataUrl: finalDataUrl,
          width: SCREEN_WIDTH,
          height: height,
        });

        console.log(`章节 ${i + 1}: ${title}, 高度 ${height}px`);

      } catch (e) {
        console.error(`章节 ${i} 转换失败:`, e);
      }
    }

    setChapterImages(images);
    setIsConverting(false);
    setConvertProgress(`完成！共 ${images.length} 个章节`);

  }, [book, config, processHtmlWithResources, toc]);

  // 等待图片加载
  const waitForImages = (container: HTMLElement | null): Promise<void> => {
    if (!container) return Promise.resolve();
    
    const images = container.querySelectorAll('img');
    const promises = Array.from(images).map(img => {
      if (img.complete) return Promise.resolve();
      return new Promise<void>(resolve => {
        img.onload = () => resolve();
        img.onerror = () => resolve();
      });
    });
    return Promise.all(promises).then(() => {});
  };

  // 下载所有图片为 ZIP
  // 生成封面图片
  const generateCoverImage = async (): Promise<string | null> => {
    if (!book) return null;

    try {
      // 方法1: 从第一章提取图片
      if (spineItems.length > 0) {
        const firstSection = book.spine.get(spineItems[0].href);
        if (firstSection) {
          const sectionData = await firstSection.load(book.load.bind(book));
          const doc = new DOMParser().parseFromString(sectionData, 'text/html');
          const firstImg = doc.querySelector('img');
          
          if (firstImg) {
            const src = firstImg.getAttribute('src');
            if (src && !src.startsWith('data:')) {
              // 提取图片
              const archive = book.archive as any;
              let basePrefix = '';
              if (archive && archive.zip) {
                const allFiles = Object.keys(archive.zip.files || {});
                const sectionFile = allFiles.find(f => f.endsWith(spineItems[0].href));
                if (sectionFile) {
                  basePrefix = sectionFile.substring(0, sectionFile.length - spineItems[0].href.length);
                }
              }
              
              const sectionDir = spineItems[0].href.substring(0, spineItems[0].href.lastIndexOf('/') + 1);
              let imagePath = src;
              if (src.startsWith('../')) {
                const parts = sectionDir.split('/').filter(p => p);
                const srcParts = src.split('/');
                let upCount = srcParts.filter(p => p === '..').length;
                imagePath = [...parts.slice(0, parts.length - upCount), ...srcParts.slice(upCount)].join('/');
              } else if (!src.startsWith('/')) {
                imagePath = sectionDir + src;
              }
              imagePath = basePrefix + imagePath;
              
              const zipFile = archive?.zip?.files?.[imagePath];
              if (zipFile) {
                const uint8Array = await zipFile.async('uint8array');
                const mimeType = imagePath.endsWith('.png') ? 'image/png' : 'image/jpeg';
                const blob = new Blob([uint8Array], { type: mimeType });
                const dataUrl = await blobToDataUrl(blob);
                return dataUrl;
              }
            }
          }
        }
      }

      // 方法2: 从资源中查找第一个图片
      const archive = book.archive as any;
      if (archive && archive.zip) {
        const imageFiles = Object.keys(archive.zip.files).filter(f => 
          /\.(jpg|jpeg|png|gif|webp)$/i.test(f) && !f.includes('__MACOSX')
        );
        
        if (imageFiles.length > 0) {
          const firstImage = imageFiles[0];
          const uint8Array = await archive.zip.files[firstImage].async('uint8array');
          const mimeType = firstImage.endsWith('.png') ? 'image/png' : 'image/jpeg';
          const blob = new Blob([uint8Array], { type: mimeType });
          const dataUrl = await blobToDataUrl(blob);
          return dataUrl;
        }
      }

      // 方法3: 渲染书籍标题
      const canvas = document.createElement('canvas');
      const size = 540; // 正方形尺寸
      canvas.width = size;
      canvas.height = size;
      const ctx = canvas.getContext('2d')!;
      
      ctx.fillStyle = '#ffffff';
      ctx.fillRect(0, 0, size, size);
      
      ctx.fillStyle = '#000000';
      ctx.font = `600 36px ${config.fontFamily}`;
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      
      const title = bookInfo?.title || 'EPUB';
      const words = title.split('');
      const lines: string[] = [];
      let currentLine = '';
      
      for (const word of words) {
        const testLine = currentLine + word;
        const metrics = ctx.measureText(testLine);
        if (metrics.width > size - 80 && currentLine) {
          lines.push(currentLine);
          currentLine = word;
        } else {
          currentLine = testLine;
        }
      }
      if (currentLine) lines.push(currentLine);
      
      const lineHeight = 50;
      const startY = (size - lines.length * lineHeight) / 2;
      lines.forEach((line, i) => {
        ctx.fillText(line, size / 2, startY + i * lineHeight + lineHeight / 2);
      });
      
      return canvas.toDataURL('image/png');
    } catch (e) {
      console.error('生成封面失败:', e);
      return null;
    }
  };

  // 裁切图片为正方形（从左上角取短边）
  const cropToSquare = async (dataUrl: string): Promise<string> => {
    return new Promise((resolve) => {
      const img = new Image();
      img.onload = () => {
        const size = Math.min(img.width, img.height);
        const canvas = document.createElement('canvas');
        canvas.width = size;
        canvas.height = size;
        const ctx = canvas.getContext('2d')!;
        
        // 从左上角裁切
        ctx.drawImage(img, 0, 0, size, size, 0, 0, size, size);
        resolve(canvas.toDataURL('image/png'));
      };
      img.src = dataUrl;
    });
  };

  const downloadAllImages = async () => {
    if (chapterImages.length === 0) return;

    const JSZip = (await import('jszip')).default;
    const zip = new JSZip();

    // 生成封面
    try {
      let coverDataUrl = await generateCoverImage();
      if (coverDataUrl) {
        // 裁切为正方形
        coverDataUrl = await cropToSquare(coverDataUrl);
        // 转换为灰度
        const grayscaleCover = await convertToGrayscale(coverDataUrl, config.grayscaleLevels);
        const base64Data = grayscaleCover.split(',')[1];
        zip.file('COVER.png', base64Data, { base64: true });
      }
    } catch (e) {
      console.error('封面生成失败:', e);
    }

    // 添加章节图片
    for (const img of chapterImages) {
      const base64Data = img.dataUrl.split(',')[1];
      const fileName = `${String(img.index + 1).padStart(3, '0')}_${img.title.replace(/[<>:"/\\|?*]/g, '_')}.png`;
      zip.file(fileName, base64Data, { base64: true });
    }

    const blob = await zip.generateAsync({ type: 'blob' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `${bookInfo?.title || 'epub'}_images.zip`;
    a.click();
    URL.revokeObjectURL(url);
  };

  // 计算总大小
  const getTotalSize = () => {
    let total = 0;
    for (const img of chapterImages) {
      total += (img.dataUrl.length - img.dataUrl.indexOf(',') - 1) * 0.75;
    }
    return (total / 1024 / 1024).toFixed(2);
  };

  // 渲染样式（用于预览）
  const previewStyle: React.CSSProperties = {
    fontFamily: config.fontFamily,
    fontSize: `${config.fontSize}px`,
    fontWeight: config.fontWeight,
    lineHeight: config.lineHeight,
    color: config.textColor,
    backgroundColor: config.backgroundColor,
    padding: `${config.paddingV}px ${config.paddingH}px`,
    overflowX: 'hidden',
  };

  return (
    <div className="epub-to-images">
      <div className="toolbar">
        <button onClick={onClose}>← 返回</button>
        <span className="title">{bookInfo?.title || '加载中...'}</span>
        <div style={{ display: 'flex', gap: '8px' }}>
          <button onClick={exportPreset}>📤 导出预设</button>
          <button onClick={importPreset}>📥 导入预设</button>
          <button onClick={resetPreset}>🔄 重置预设</button>
        </div>
      </div>

      {/* 配置面板 */}
      <div className="config-panel">
          <h3>渲染设置</h3>
          
          <div className="config-row">
            <label>字体：</label>
            <select
              value={config.fontFamily}
              onChange={e => updateConfig('fontFamily', e.target.value)}
            >
              {fontOptions.map(opt => (
                <option key={opt.value} value={opt.value}>{opt.label}</option>
              ))}
            </select>
          </div>

          <div className="config-row">
            <label>字号：{config.fontSize}px</label>
            <input
              type="range"
              min="12"
              max="28"
              value={config.fontSize}
              onChange={e => updateConfig('fontSize', parseInt(e.target.value))}
            />
          </div>

          <div className="config-row">
            <label>字重：{config.fontWeight}</label>
            <input
              type="range"
              min="100"
              max="900"
              step="100"
              value={config.fontWeight}
              onChange={e => updateConfig('fontWeight', parseInt(e.target.value))}
            />
          </div>

          <div className="config-row">
            <label>行高：{config.lineHeight}</label>
            <input
              type="range"
              min="1.2"
              max="2.5"
              step="0.1"
              value={config.lineHeight}
              onChange={e => updateConfig('lineHeight', parseFloat(e.target.value))}
            />
          </div>

          <div className="config-row">
            <label>水平边距：{config.paddingH}px</label>
            <input
              type="range"
              min="10"
              max="50"
              value={config.paddingH}
              onChange={e => updateConfig('paddingH', parseInt(e.target.value))}
            />
          </div>

          <div className="config-row">
            <label>垂直边距：{config.paddingV}px</label>
            <input
              type="range"
              min="10"
              max="50"
              value={config.paddingV}
              onChange={e => updateConfig('paddingV', parseInt(e.target.value))}
            />
          </div>

          <div className="config-row">
            <label>
              <input
                type="checkbox"
                checked={config.imageGrayscale}
                onChange={e => updateConfig('imageGrayscale', e.target.checked)}
              />
              输出灰度图片 ({config.grayscaleLevels} 级)
            </label>
          </div>
        </div>

      <div className="main-content">
        {/* 左侧目录 */}
        <div className="toc-sidebar">
          <h3>章节 ({spineItems.length})</h3>
          <ul>
            {spineItems.map((item, idx) => {
              const tocItem = toc.find(t => item.href?.includes(t.href.split('#')[0]));
              return (
                <li
                  key={idx}
                  className={idx === currentSpineIndex ? 'active' : ''}
                  onClick={() => book && displaySection(book, idx)}
                >
                  {tocItem?.label || `Section ${idx + 1}`}
                </li>
              );
            })}
          </ul>
        </div>

        {/* 中间预览 */}
        <div className="preview-area">
          <div className="preview-header">
            <span>预览 (宽度: {SCREEN_WIDTH}px)</span>
            <div className="nav-buttons">
              <button
                disabled={currentSpineIndex === 0}
                onClick={() => book && displaySection(book, currentSpineIndex - 1)}
              >
                上一章
              </button>
              <button
                disabled={currentSpineIndex >= spineItems.length - 1}
                onClick={() => book && displaySection(book, currentSpineIndex + 1)}
              >
                下一章
              </button>
            </div>
          </div>
          <div
            className="render-area"
            ref={renderRef}
            style={previewStyle}
            dangerouslySetInnerHTML={{ __html: currentHtml }}
          />
        </div>

        {/* 右侧操作区 */}
        <div className="action-sidebar">
          <h3>文本处理</h3>
          
          {processorManager.getProcessors().map((processor, idx) => (
            <div key={idx} className="processor-checkbox">
              <label>
                <input
                  type="checkbox"
                  checked={processor.enabled}
                  onChange={e => {
                    processor.enabled = e.target.checked;
                    saveProcessorConfig();
                    forceUpdate({}); // 触发重渲染
                  }}
                />
                {processor.name}
              </label>
              
              {/* 选中后展开配置 */}
              {processor.enabled && (
                <ConfigRenderer 
                  processor={processor} 
                  onConfigChange={() => {
                    saveProcessorConfig();
                    forceUpdate({});
                  }} 
                  onConfigChange={() => forceUpdate({})}
                />
              )}
            </div>
          ))}

          <h3>转换</h3>
          
          <button
            className="convert-btn"
            onClick={startConversion}
            disabled={isConverting || !book}
          >
            {isConverting ? '转换中...' : '开始转换'}
          </button>

          {convertProgress && (
            <p className="progress">{convertProgress}</p>
          )}

          {chapterImages.length > 0 && (
            <>
              <div className="stats">
                <p>共 {chapterImages.length} 个章节</p>
                <p>总大小约 {getTotalSize()} MB</p>
              </div>

              <button className="download-btn" onClick={downloadAllImages}>
                📦 下载 ZIP
              </button>

              <h4>章节预览</h4>
              <div className="chapter-list">
                {chapterImages.map(img => (
                  <div key={img.index} className="chapter-item">
                    <img src={img.dataUrl} alt={img.title} />
                    <div className="chapter-info">
                      <span className="chapter-title">{img.title}</span>
                      <span className="chapter-size">{img.height}px</span>
                    </div>
                  </div>
                ))}
              </div>
            </>
          )}
        </div>
      </div>
    </div>
  );
};

export default EpubToImages;
