/**
 * M5PaperS3 设备 API 客户端
 * 基于 HTTP REST API 与设备通信
 */

// ============ 类型定义 ============

export type ConnectionType = 'http';

export interface DeviceInfo {
  device: string;
  ip: string;
  wifi: {
    ssid: string;
    rssi: number;
  };
  storage: {
    total: number;
    free: number;
    used: number;
  };
}

export interface FileItem {
  name: string;
  type: 'file' | 'directory';
  size?: number;
}

export interface ListResult {
  path: string;
  items: FileItem[];
}

/** 章节信息 */
export interface SectionInfo {
  index: number;
  title: string;
  pageCount: number;
}

/** 图书元数据 (设备端格式) */
export interface BookMetadata {
  id: string;
  title: string;
  author?: string;
  sections: SectionInfo[];
  coverUrl?: string;
}

/** 上传进度回调 */
export type UploadProgressCallback = (stage: string, percent: number) => void;

/** 链接信息 */
export interface LinkInfo {
  text: string;
  rect: { x: number; y: number; width: number; height: number };
  href: string;
  type: 'internal' | 'external';
  target?: { section: number; page: number };
}

/** 页面链接信息 */
export interface PageLinks {
  page: number;
  hasImage: boolean;  // 该页是否包含图片
  links: LinkInfo[];
}

// ============ API 接口 ============

export interface IDeviceClient {
  /** 测试连接 */
  testConnection(): Promise<boolean>;
  
  /** 获取设备信息 */
  getDeviceInfo(): Promise<DeviceInfo>;
  
  /** 获取图书列表 */
  getBookList(): Promise<BookMetadata[]>;
  
  /** 删除图书 */
  deleteBook(bookId: string): Promise<void>;
  
  /** 上传图书 (PNG图片格式 + 链接信息) */
  uploadBook(
    bookId: string,
    title: string,
    author: string | undefined,
    coverBlob: Blob | null,
    sections: Array<{
      index: number;
      title: string;
      pages: Blob[];  // PNG 图片数组
      pageLinks?: PageLinks[];  // 链接信息（可选）
    }>,
    anchorMap?: Record<string, { section: number; page: number }>,
    onProgress?: UploadProgressCallback
  ): Promise<void>;
}

// ============ HTTP 客户端实现 ============

class HttpDeviceClient implements IDeviceClient {
  private baseUrl: string = '';

  setDevice(ip: string) {
    // 移除可能的协议前缀和尾部斜杠
    ip = ip.replace(/^https?:\/\//, '').replace(/\/$/, '');
    this.baseUrl = `http://${ip}`;
  }

  async testConnection(): Promise<boolean> {
    try {
      const response = await fetch(`${this.baseUrl}/api/info`, {
        signal: AbortSignal.timeout(5000),
      });
      return response.ok;
    } catch {
      return false;
    }
  }

  async getDeviceInfo(): Promise<DeviceInfo> {
    const response = await fetch(`${this.baseUrl}/api/info`);
    if (!response.ok) {
      throw new Error(`获取设备信息失败: ${response.status}`);
    }
    return response.json();
  }

  async listDirectory(path: string): Promise<ListResult> {
    const response = await fetch(
      `${this.baseUrl}/api/list?path=${encodeURIComponent(path)}`
    );
    if (!response.ok) {
      throw new Error(`列出目录失败: ${response.status}`);
    }
    return response.json();
  }

  async getBookList(): Promise<BookMetadata[]> {
    const books: BookMetadata[] = [];
    
    try {
      const result = await this.listDirectory('/books');
      
      for (const item of result.items) {
        if (item.type !== 'directory') continue;
        
        const bookId = item.name;
        
        try {
          // 读取 metadata.json
          const metadataResponse = await fetch(
            `${this.baseUrl}/api/file?path=${encodeURIComponent(`/books/${bookId}/metadata.json`)}`
          );
          
          if (!metadataResponse.ok) continue;
          
          const metadata = await metadataResponse.json();
          
          books.push({
            id: bookId,
            title: metadata.title || bookId,
            author: metadata.author,
            sections: metadata.sections || [],
            coverUrl: `${this.baseUrl}/api/file?path=${encodeURIComponent(`/books/${bookId}/cover.png`)}`,
          });
        } catch (e) {
          console.warn(`读取图书 ${bookId} 元数据失败:`, e);
        }
      }
    } catch (e) {
      console.error('获取图书列表失败:', e);
    }
    
    return books;
  }

  async deleteBook(bookId: string): Promise<void> {
    const response = await fetch(
      `${this.baseUrl}/api/rmdir?path=${encodeURIComponent(`/books/${bookId}`)}`,
      { method: 'DELETE' }
    );
    
    if (!response.ok) {
      throw new Error(`删除图书失败: ${response.status}`);
    }
  }

  async uploadBook(
    bookId: string,
    title: string,
    author: string | undefined,
    coverBlob: Blob | null,
    sections: Array<{
      index: number;
      title: string;
      pages: Blob[];
      pageLinks?: PageLinks[];
    }>,
    anchorMap?: Record<string, { section: number; page: number }>,
    onProgress?: UploadProgressCallback
  ): Promise<void> {
    const bookPath = `/books/${bookId}`;
    
    // 1. 创建目录结构
    onProgress?.('创建目录', 0);
    await this.createDirectory(bookPath);
    await this.createDirectory(`${bookPath}/sections`);
    
    // 为每个章节创建目录
    for (const section of sections) {
      const sectionDir = `${bookPath}/sections/${String(section.index).padStart(3, '0')}`;
      await this.createDirectory(sectionDir);
    }
    
    // 2. 准备所有文件
    const filesToUpload: Array<{ path: string; blob: Blob }> = [];
    
    // 封面
    if (coverBlob) {
      filesToUpload.push({ path: 'cover.png', blob: coverBlob });
    }
    
    // 章节图片
    for (const section of sections) {
      const sectionDir = `sections/${String(section.index).padStart(3, '0')}`;
      for (let i = 0; i < section.pages.length; i++) {
        const pageFilename = `${String(i + 1).padStart(3, '0')}.png`;
        filesToUpload.push({
          path: `${sectionDir}/${pageFilename}`,
          blob: section.pages[i],
        });
      }
    }
    
    // 3. 使用批量上传
    onProgress?.('上传文件', 10);
    
    // 分批上传，每批最多40个文件（优化后的图片更小，设备buffer已增大）
    const BATCH_SIZE = 40;
    const totalFiles = filesToUpload.length;
    let uploadedFiles = 0;
    
    for (let i = 0; i < filesToUpload.length; i += BATCH_SIZE) {
      const batch = filesToUpload.slice(i, i + BATCH_SIZE);
      
      const formData = new FormData();
      for (const file of batch) {
        formData.append('file', file.blob, file.path);
      }
      
      const response = await fetch(
        `${this.baseUrl}/api/upload-batch?dir=${encodeURIComponent(bookPath)}`,
        {
          method: 'POST',
          body: formData,
        }
      );
      
      if (!response.ok) {
        throw new Error(`批量上传失败: ${response.status}`);
      }
      
      uploadedFiles += batch.length;
      const percent = 10 + (uploadedFiles / totalFiles) * 80;
      onProgress?.('上传文件', percent);
    }
    
    // 4. 处理链接目标解析
    onProgress?.('处理链接信息', 85);
    
    // 构建锚点映射
    const resolvedAnchorMap: Record<string, { section: number; page: number }> = 
      anchorMap ? { ...anchorMap } : {};
    
    // 解析每个章节的链接，填充 target 字段
    for (const section of sections) {
      if (section.pageLinks) {
        for (const pageLink of section.pageLinks) {
          for (const link of pageLink.links) {
            if (link.type === 'internal' && link.href.startsWith('#')) {
              const anchorId = link.href.substring(1); // 移除 '#'
              const target = resolvedAnchorMap[anchorId];
              if (target) {
                link.target = target;
              }
            }
          }
        }
      }
    }
    
    // 5. 上传每个章节的 links.json（包含页面元数据）
    onProgress?.('上传页面元数据', 90);
    
    for (const section of sections) {
      // 始终上传 links.json，即使没有链接（因为包含 hasImage 信息）
      if (section.pageLinks && section.pageLinks.length > 0) {
        const linksData = {
          pages: section.pageLinks
        };
        
        const linksBlob = new Blob(
          [JSON.stringify(linksData, null, 2)],
          { type: 'application/json' }
        );
        
        const sectionDir = `${bookPath}/sections/${String(section.index).padStart(3, '0')}`;
        await this.uploadFile(linksBlob, `${sectionDir}/links.json`);
      }
    }
    
    // 6. 创建 metadata.json
    onProgress?.('保存元数据', 95);
    
    const metadata: any = {
      id: bookId,
      title,
      author,
      sections: sections.map(s => ({
        index: s.index,
        title: s.title,
        pageCount: s.pages.length,
      })),
    };
    
    // 添加锚点映射（如果存在）
    if (resolvedAnchorMap && Object.keys(resolvedAnchorMap).length > 0) {
      metadata.anchorMap = resolvedAnchorMap;
    }
    
    const metadataBlob = new Blob(
      [JSON.stringify(metadata, null, 2)],
      { type: 'application/json' }
    );
    
    await this.uploadFile(metadataBlob, `${bookPath}/metadata.json`);
    
    onProgress?.('完成', 100);
  }

  private async createDirectory(path: string): Promise<void> {
    const response = await fetch(
      `${this.baseUrl}/api/mkdir?path=${encodeURIComponent(path)}`,
      { method: 'POST' }
    );
    
    if (!response.ok) {
      const text = await response.text();
      // 目录已存在不算错误
      if (!text.includes('exist')) {
        throw new Error(`创建目录失败: ${response.status}`);
      }
    }
  }

  private async uploadFile(blob: Blob, path: string): Promise<void> {
    const response = await fetch(
      `${this.baseUrl}/api/file?path=${encodeURIComponent(path)}`,
      {
        method: 'POST',
        body: blob,
      }
    );
    
    if (!response.ok) {
      throw new Error(`上传文件失败: ${response.status}`);
    }
  }
}

// ============ 导出 ============

// 单例
const httpClient = new HttpDeviceClient();

export function getHttpClient(): HttpDeviceClient {
  return httpClient;
}

