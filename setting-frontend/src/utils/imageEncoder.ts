/**
 * QOI (Quite OK Image) 编码器
 * 规范: https://qoiformat.org/
 */

const QOI_MAGIC = 'qoif';
const QOI_HEADER_SIZE = 14;
const QOI_PADDING = [0, 0, 0, 0, 0, 0, 0, 1];

const QOI_OP_RGB = 0xfe;
const QOI_OP_RGBA = 0xff;
const QOI_OP_INDEX = 0x00;
const QOI_OP_DIFF = 0x40;
const QOI_OP_LUMA = 0x80;
const QOI_OP_RUN = 0xc0;

interface RGBA {
  r: number;
  g: number;
  b: number;
  a: number;
}

function colorHash(color: RGBA): number {
  return (color.r * 3 + color.g * 5 + color.b * 7 + color.a * 11) % 64;
}

/**
 * 将 Canvas 编码为 QOI 格式
 */
export async function encodeToQOI(canvas: HTMLCanvasElement): Promise<Blob> {
  const ctx = canvas.getContext('2d');
  if (!ctx) {
    throw new Error('无法获取 Canvas 上下文');
  }

  const width = canvas.width;
  const height = canvas.height;
  const imageData = ctx.getImageData(0, 0, width, height);
  const pixels = imageData.data;

  // 估算最大输出大小
  const maxSize = QOI_HEADER_SIZE + width * height * 5 + QOI_PADDING.length;
  const bytes = new Uint8Array(maxSize);
  let p = 0;

  // 写入头部
  // Magic
  bytes[p++] = QOI_MAGIC.charCodeAt(0);
  bytes[p++] = QOI_MAGIC.charCodeAt(1);
  bytes[p++] = QOI_MAGIC.charCodeAt(2);
  bytes[p++] = QOI_MAGIC.charCodeAt(3);

  // Width (32-bit big endian)
  bytes[p++] = (width >> 24) & 0xff;
  bytes[p++] = (width >> 16) & 0xff;
  bytes[p++] = (width >> 8) & 0xff;
  bytes[p++] = width & 0xff;

  // Height (32-bit big endian)
  bytes[p++] = (height >> 24) & 0xff;
  bytes[p++] = (height >> 16) & 0xff;
  bytes[p++] = (height >> 8) & 0xff;
  bytes[p++] = height & 0xff;

  // Channels (3 = RGB, 4 = RGBA)
  bytes[p++] = 4;

  // Colorspace (0 = sRGB with linear alpha, 1 = all channels linear)
  bytes[p++] = 0;

  // 编码像素数据
  const index: RGBA[] = new Array(64);
  for (let i = 0; i < 64; i++) {
    index[i] = { r: 0, g: 0, b: 0, a: 0 };
  }

  let prevPixel: RGBA = { r: 0, g: 0, b: 0, a: 255 };
  let run = 0;

  for (let i = 0; i < pixels.length; i += 4) {
    const pixel: RGBA = {
      r: pixels[i],
      g: pixels[i + 1],
      b: pixels[i + 2],
      a: pixels[i + 3],
    };

    if (
      pixel.r === prevPixel.r &&
      pixel.g === prevPixel.g &&
      pixel.b === prevPixel.b &&
      pixel.a === prevPixel.a
    ) {
      run++;
      if (run === 62 || i === pixels.length - 4) {
        bytes[p++] = QOI_OP_RUN | (run - 1);
        run = 0;
      }
    } else {
      if (run > 0) {
        bytes[p++] = QOI_OP_RUN | (run - 1);
        run = 0;
      }

      const indexPos = colorHash(pixel);

      if (
        index[indexPos].r === pixel.r &&
        index[indexPos].g === pixel.g &&
        index[indexPos].b === pixel.b &&
        index[indexPos].a === pixel.a
      ) {
        bytes[p++] = QOI_OP_INDEX | indexPos;
      } else {
        index[indexPos] = { ...pixel };

        if (pixel.a === prevPixel.a) {
          const vr = pixel.r - prevPixel.r;
          const vg = pixel.g - prevPixel.g;
          const vb = pixel.b - prevPixel.b;

          const vg_r = vr - vg;
          const vg_b = vb - vg;

          if (
            vr > -3 &&
            vr < 2 &&
            vg > -3 &&
            vg < 2 &&
            vb > -3 &&
            vb < 2
          ) {
            bytes[p++] = QOI_OP_DIFF | ((vr + 2) << 4) | ((vg + 2) << 2) | (vb + 2);
          } else if (
            vg_r > -9 &&
            vg_r < 8 &&
            vg > -33 &&
            vg < 32 &&
            vg_b > -9 &&
            vg_b < 8
          ) {
            bytes[p++] = QOI_OP_LUMA | (vg + 32);
            bytes[p++] = ((vg_r + 8) << 4) | (vg_b + 8);
          } else {
            bytes[p++] = QOI_OP_RGB;
            bytes[p++] = pixel.r;
            bytes[p++] = pixel.g;
            bytes[p++] = pixel.b;
          }
        } else {
          bytes[p++] = QOI_OP_RGBA;
          bytes[p++] = pixel.r;
          bytes[p++] = pixel.g;
          bytes[p++] = pixel.b;
          bytes[p++] = pixel.a;
        }
      }

      prevPixel = { ...pixel };
    }
  }

  // 写入 padding
  for (let i = 0; i < QOI_PADDING.length; i++) {
    bytes[p++] = QOI_PADDING[i];
  }

  // 返回实际大小的数据
  return new Blob([bytes.slice(0, p)], { type: 'application/octet-stream' });
}

/**
 * 将 Canvas 编码为 PNG 格式
 */
export async function encodeToPNG(canvas: HTMLCanvasElement): Promise<Blob> {
  return new Promise((resolve, reject) => {
    canvas.toBlob(
      (blob) => {
        if (blob) {
          resolve(blob);
        } else {
          reject(new Error('无法生成 PNG'));
        }
      },
      'image/png',
      1.0
    );
  });
}
