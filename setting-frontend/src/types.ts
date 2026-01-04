/** 目标屏幕尺寸配置 */
export const TARGET_WIDTH = 540;
export const TARGET_HEIGHT = 940;

/** 图片输出格式 */
export enum ImageFormat {
  PNG = 'png',
  QOI = 'qoi',
}

/** 处理状态 */
export enum ProcessStatus {
  IDLE = 'idle',
  LOADING = 'loading',
  PARSING = 'parsing',
  PAGINATING = 'paginating',
  RENDERING = 'rendering',
  ENCODING = 'encoding',
  UPLOADING = 'uploading',
  COMPLETED = 'completed',
  ERROR = 'error',
}

/** 书籍元信息 */
export interface BookMetadata {
  title: string;
  author?: string;
  pageCount: number;
  width: number;
  height: number;
  format: ImageFormat;
}

/** 处理进度 */
export interface ProcessProgress {
  status: ProcessStatus;
  currentPage?: number;
  totalPages?: number;
  message?: string;
  error?: string;
}

/** 页面数据 */
export interface PageData {
  index: number;
  imageData: Blob;
  format: ImageFormat;
}
