/**
 * 设备 API 客户端
 * 用于与 ESP32-S3 PaperS3 设备通信
 */

export interface DeviceInfo {
  ip: string;
  port: number;
}

export class DeviceClient {
  private baseUrl: string;

  constructor(device: DeviceInfo) {
    this.baseUrl = `http://${device.ip}:${device.port}`;
  }

  /**
   * 上传书籍元信息
   */
  async uploadMetadata(metadata: unknown): Promise<void> {
    const response = await fetch(`${this.baseUrl}/api/book/metadata`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify(metadata),
    });

    if (!response.ok) {
      throw new Error(`上传元信息失败: ${response.statusText}`);
    }
  }

  /**
   * 上传单个页面图片
   */
  async uploadPage(pageIndex: number, imageData: Blob): Promise<void> {
    const formData = new FormData();
    formData.append('page', imageData, `${pageIndex}.qoi`);

    const response = await fetch(`${this.baseUrl}/api/book/page/${pageIndex}`, {
      method: 'POST',
      body: formData,
    });

    if (!response.ok) {
      throw new Error(`上传页面 ${pageIndex} 失败: ${response.statusText}`);
    }
  }

  /**
   * 通知设备处理完成
   */
  async notifyComplete(): Promise<void> {
    const response = await fetch(`${this.baseUrl}/api/book/complete`, {
      method: 'POST',
    });

    if (!response.ok) {
      throw new Error(`通知完成失败: ${response.statusText}`);
    }
  }

  /**
   * 获取设备状态
   */
  async getStatus(): Promise<unknown> {
    const response = await fetch(`${this.baseUrl}/api/status`);

    if (!response.ok) {
      throw new Error(`获取状态失败: ${response.statusText}`);
    }

    return await response.json();
  }
}
