import ePub, { Book, Rendition } from 'epubjs';
import { ProcessProgress, ProcessStatus, ImageFormat } from '../types';
import { TARGET_WIDTH, TARGET_HEIGHT } from '../types';
import { renderPageToCanvas } from './pageRenderer';
import { encodeToQOI, encodeToPNG } from '../utils/imageEncoder';

interface ProcessResult {
  pageUrls: string[];
  bookTitle: string;
  download: () => void;
}

/**
 * 计算 Canvas 内容的哈希值（用于检测真正的重复）
 * 使用更多采样点和更精确的算法
 */
async function getCanvasHash(canvas: HTMLCanvasElement): Promise<string> {
  const ctx = canvas.getContext('2d');
  if (!ctx) return '';
  
  const width = canvas.width;
  const height = canvas.height;
  
  // 获取整个图像数据并采样计算哈希
  const imageData = ctx.getImageData(0, 0, width, height);
  const data = imageData.data;
  
  // 采样计算简单哈希
  let hash = 0;
  const step = Math.floor(data.length / 1000); // 采样约1000个点
  for (let i = 0; i < data.length; i += step * 4) {
    hash = ((hash << 5) - hash + data[i]) | 0;
    hash = ((hash << 5) - hash + data[i + 1]) | 0;
    hash = ((hash << 5) - hash + data[i + 2]) | 0;
  }
  
  return hash.toString(16);
}

/**
 * 检查页面是否为空白（全白或几乎全白）
 */
function isBlankPage(canvas: HTMLCanvasElement): boolean {
  const ctx = canvas.getContext('2d');
  if (!ctx) return true;
  
  const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
  const data = imageData.data;
  
  let nonWhitePixels = 0;
  const step = 100; // 采样
  
  for (let i = 0; i < data.length; i += step * 4) {
    // 检查是否接近白色 (RGB > 250)
    if (data[i] < 250 || data[i + 1] < 250 || data[i + 2] < 250) {
      nonWhitePixels++;
    }
  }
  
  // 如果非白色像素少于 1%，认为是空白页
  const totalSamples = Math.floor(data.length / (step * 4));
  return nonWhitePixels / totalSamples < 0.01;
}

export async function processEpubFile(
  file: File,
  format: ImageFormat,
  onProgress: (progress: ProcessProgress) => void
): Promise<ProcessResult> {
  // 1. 加载 EPUB
  onProgress({
    status: ProcessStatus.LOADING,
    message: '正在加载 EPUB 文件...',
  });

  const arrayBuffer = await file.arrayBuffer();
  const book: Book = ePub(arrayBuffer);

  // 2. 解析元数据
  onProgress({
    status: ProcessStatus.PARSING,
    message: '正在解析书籍内容...',
  });

  await book.ready;
  const metadata = await book.loaded.metadata;

  console.log('Book metadata:', metadata);

  // 3. 生成位置信息（关键步骤！）
  onProgress({
    status: ProcessStatus.PAGINATING,
    message: '正在计算分页位置...',
  });

  // 根据页面大小计算每页大约的字符数
  // 540x940 页面，40px 字体，大约每行 12 个字，约 18 行，即约 216 字符
  const charsPerPage = 250;
  await book.locations.generate(charsPerPage);
  
  const totalLocations = book.locations.length();
  console.log('Total locations:', totalLocations);

  // 4. 创建渲染器
  const container = document.createElement('div');
  container.style.width = `${TARGET_WIDTH}px`;
  container.style.height = `${TARGET_HEIGHT}px`;
  container.style.position = 'fixed';
  container.style.left = '-9999px';
  container.style.top = '0';
  document.body.appendChild(container);

  const rendition: Rendition = book.renderTo(container, {
    width: TARGET_WIDTH,
    height: TARGET_HEIGHT,
    flow: 'paginated',
    spread: 'none',
  });

  // 注入自定义 CSS（强制统一排版）- 字体 40px
  rendition.themes.default({
    body: {
      margin: '0 !important',
      padding: '28px 20px !important',
      'font-family': '"Source Han Serif", "Noto Serif SC", "Noto Sans SC", "Microsoft YaHei", "PingFang SC", serif !important',
      'font-size': '40px !important',
      'line-height': '1.55 !important',
      color: '#000 !important',
      background: '#fff !important',
    },
    'p, div, span': {
      'font-size': 'inherit !important',
      'line-height': 'inherit !important',
    },
    p: {
      margin: '0 0 0.7em 0 !important',
      'text-indent': '0 !important',
    },
    img: {
      'max-width': '100% !important',
      'max-height': '80vh !important',
      display: 'block !important',
      margin: '0.8em auto !important',
    },
    h1: { 'font-size': '1.3em !important', margin: '0.5em 0 !important' },
    h2: { 'font-size': '1.15em !important', margin: '0.4em 0 !important' },
    h3: { 'font-size': '1.05em !important', margin: '0.4em 0 !important' },
  });

  // 4. 开始渲染（不预估总页数，动态更新）
  onProgress({
    status: ProcessStatus.PAGINATING,
    message: '正在开始渲染...',
  });

  await rendition.display();

  console.log('开始渲染页面');

  // 5. 渲染每一页
  const pages: Blob[] = [];
  const pageUrls: string[] = [];
  let currentPage = 0;
  const pageHashes = new Set<string>(); // 使用图片内容哈希检测重复
  let lastCfi = '';
  let sameLocationCount = 0;

  // 遍历所有位置
  let location = rendition.currentLocation() as any;
  
  while (location) {
    try {
      // 获取当前 CFI（兼容不同版本的 epub.js）
      const currentCfi = location?.start?.cfi || location?.cfi || '';
      
      // 检查 CFI 是否变化
      if (currentCfi === lastCfi) {
        sameLocationCount++;
        if (sameLocationCount >= 3) {
          console.log('CFI 未变化 3 次，判定到达末尾');
          break;
        }
      } else {
        sameLocationCount = 0;
        lastCfi = currentCfi;
      }
      
      // 检查是否到达末尾
      if (location?.atEnd === true) {
        console.log('atEnd 标记为 true，到达末尾');
        break;
      }
      
      // 等待渲染稳定
      await new Promise(resolve => setTimeout(resolve, 250));

      // 渲染当前页面到 Canvas
      const canvas = await renderPageToCanvas(rendition, TARGET_WIDTH, TARGET_HEIGHT);
      
      // 检查是否空白页
      if (isBlankPage(canvas)) {
        console.log('跳过空白页');
        await rendition.next();
        location = rendition.currentLocation();
        continue;
      }
      
      // 计算图片内容哈希
      const imageHash = await getCanvasHash(canvas);
      
      // 检查是否为重复页面（内容完全相同）
      if (pageHashes.has(imageHash)) {
        console.log('跳过重复页面 (哈希相同):', imageHash);
        await rendition.next();
        location = rendition.currentLocation();
        continue;
      }
      
      pageHashes.add(imageHash);
      currentPage++;

      // 更新进度
      onProgress({
        status: ProcessStatus.RENDERING,
        currentPage: currentPage,
        message: `正在渲染第 ${currentPage} 页...`,
      });

      // 编码为图片
      let blob: Blob;
      if (format === ImageFormat.QOI) {
        blob = await encodeToQOI(canvas);
      } else {
        blob = await encodeToPNG(canvas);
      }

      pages.push(blob);
      
      // 创建预览用的 URL
      const url = URL.createObjectURL(blob);
      pageUrls.push(url);

      // 翻到下一页
      await rendition.next();
      location = rendition.currentLocation();
      
      // 安全检查：如果页数过多，防止无限循环
      if (currentPage > 10000) {
        console.warn('页数超过 10000，停止渲染');
        break;
      }
    } catch (error) {
      console.error('Error rendering page:', error);
      break;
    }
  }

  // 更新最终页数
  onProgress({
    status: ProcessStatus.RENDERING,
    currentPage: currentPage,
    totalPages: currentPage,
    message: `渲染完成，共 ${currentPage} 页`,
  });

  // 6. 清理
  document.body.removeChild(container);
  rendition.destroy();

  // 7. 返回结果
  const bookTitle = metadata.title || 'book';
  
  const download = async () => {
    onProgress({
      status: ProcessStatus.ENCODING,
      message: '正在打包图片...',
    });
    await downloadPages(pages, bookTitle, format);
  };

  return {
    pageUrls,
    bookTitle,
    download,
  };
}

async function downloadPages(pages: Blob[], bookTitle: string, format: ImageFormat) {
  // 使用 JSZip 打包所有页面
  const JSZip = (await import('jszip')).default;
  const zip = new JSZip();

  const pagesFolder = zip.folder('pages');
  if (!pagesFolder) return;

  pages.forEach((blob, index) => {
    const pageNumber = String(index + 1).padStart(4, '0');
    pagesFolder.file(`${pageNumber}.${format}`, blob);
  });

  // 添加 manifest.json
  const manifest = {
    title: bookTitle,
    pageCount: pages.length,
    width: TARGET_WIDTH,
    height: TARGET_HEIGHT,
    format: format,
  };

  zip.file('manifest.json', JSON.stringify(manifest, null, 2));

  // 生成并下载
  const zipBlob = await zip.generateAsync({ type: 'blob' });
  const url = URL.createObjectURL(zipBlob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `${bookTitle}-pages.zip`;
  a.click();
  URL.revokeObjectURL(url);
}
