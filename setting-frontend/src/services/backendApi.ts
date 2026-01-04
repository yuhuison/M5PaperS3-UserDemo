const API_BASE = 'http://localhost:3001';

export interface ConvertConfig {
  width?: number;
  height?: number;
  fontSize?: number;
  lineHeight?: number;
  padding?: number;
}

export interface ConvertResult {
  success: boolean;
  blob?: Blob;
  error?: string;
}

/**
 * 调用后端 API 将 EPUB 转换为图片序列
 */
export async function convertEpubViaBackend(
  file: File,
  config: ConvertConfig = {},
  onProgress?: (message: string) => void
): Promise<Blob> {
  const formData = new FormData();
  formData.append('epub', file);
  
  // 添加配置参数
  formData.append('width', String(config.width || 540));
  formData.append('height', String(config.height || 940));
  formData.append('fontSize', String(config.fontSize || 40));
  formData.append('lineHeight', String(config.lineHeight || 1.6));
  formData.append('padding', String(config.padding || 20));

  onProgress?.('正在上传文件到服务器...');

  const response = await fetch(`${API_BASE}/api/convert`, {
    method: 'POST',
    body: formData,
  });

  if (!response.ok) {
    const error = await response.json().catch(() => ({ message: '转换失败' }));
    throw new Error(error.message || '转换失败');
  }

  onProgress?.('正在下载转换结果...');
  
  const blob = await response.blob();
  return blob;
}

/**
 * 检查后端服务是否可用
 */
export async function checkBackendHealth(): Promise<boolean> {
  try {
    const response = await fetch(`${API_BASE}/api/health`, {
      method: 'GET',
      signal: AbortSignal.timeout(3000),
    });
    return response.ok;
  } catch {
    return false;
  }
}

/**
 * 从 ZIP blob 中提取图片用于预览
 */
export async function extractImagesFromZip(zipBlob: Blob): Promise<{
  pageUrls: string[];
  manifest: { pageCount: number };
}> {
  const JSZip = (await import('jszip')).default;
  const zip = await JSZip.loadAsync(zipBlob);
  
  // 读取 manifest
  const manifestFile = zip.file('manifest.json');
  const manifest = manifestFile 
    ? JSON.parse(await manifestFile.async('string'))
    : { pageCount: 0, pages: [] };

  // 提取所有图片并创建 URL
  const pageUrls: string[] = [];
  
  // 按顺序提取
  const files = Object.keys(zip.files)
    .filter(name => name.endsWith('.png'))
    .sort();

  for (const filename of files) {
    const file = zip.file(filename);
    if (file) {
      const blob = await file.async('blob');
      pageUrls.push(URL.createObjectURL(blob));
    }
  }

  return { pageUrls, manifest };
}
