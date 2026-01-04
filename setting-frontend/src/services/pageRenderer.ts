import { Rendition } from 'epubjs';

/**
 * 将当前渲染的页面转换为 Canvas
 */
export async function renderPageToCanvas(
  rendition: Rendition,
  width: number,
  height: number
): Promise<HTMLCanvasElement> {
  const canvas = document.createElement('canvas');
  canvas.width = width;
  canvas.height = height;
  const ctx = canvas.getContext('2d');

  if (!ctx) {
    throw new Error('无法获取 Canvas 上下文');
  }

  // 填充白色背景
  ctx.fillStyle = '#FFFFFF';
  ctx.fillRect(0, 0, width, height);

  // 获取 epub.js 渲染的 iframe
  const iframe = rendition.manager?.container?.querySelector('iframe');
  if (!iframe || !iframe.contentWindow) {
    throw new Error('无法找到渲染的 iframe');
  }

  // 使用 html2canvas 或类似方法将 iframe 内容绘制到 Canvas
  // 这里简化处理：直接截取 iframe 的截图
  await captureIframeToCanvas(iframe, ctx, width, height);

  return canvas;
}

/**
 * 截取 iframe 内容到 Canvas
 * 注意：由于浏览器安全限制，这里使用简化方法
 */
async function captureIframeToCanvas(
  iframe: HTMLIFrameElement,
  ctx: CanvasRenderingContext2D,
  width: number,
  height: number
): Promise<void> {
  try {
    const iframeDoc = iframe.contentDocument || iframe.contentWindow?.document;
    if (!iframeDoc) {
      throw new Error('无法访问 iframe 文档');
    }

    const body = iframeDoc.body;
    if (!body) {
      throw new Error('iframe 文档无 body');
    }

    // 使用 html2canvas 渲染（需要安装 html2canvas 库）
    // 这里提供一个简化版本：直接复制文本内容
    // 实际项目中应使用专业的截图库
    
    // 临时方案：使用 foreignObject + SVG
    const serializer = new XMLSerializer();
    const html = serializer.serializeToString(body);
    
    const svg = `
      <svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}">
        <foreignObject width="100%" height="100%">
          ${html}
        </foreignObject>
      </svg>
    `;

    const img = new Image();
    const svgBlob = new Blob([svg], { type: 'image/svg+xml;charset=utf-8' });
    const url = URL.createObjectURL(svgBlob);

    await new Promise<void>((resolve, reject) => {
      img.onload = () => {
        ctx.drawImage(img, 0, 0, width, height);
        URL.revokeObjectURL(url);
        resolve();
      };
      img.onerror = () => {
        URL.revokeObjectURL(url);
        reject(new Error('无法加载 SVG'));
      };
      img.src = url;
    });
  } catch (error) {
    console.error('截取 iframe 失败:', error);
    // 降级方案：绘制提示文本
    ctx.fillStyle = '#000000';
    ctx.font = '16px sans-serif';
    ctx.fillText('页面渲染中...', 20, 30);
  }
}
