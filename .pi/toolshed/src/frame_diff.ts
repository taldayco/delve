import { readFileSync } from "node:fs";
import { inflateSync } from "node:zlib";

// ─── PNG Decoder (zero dependencies) ─────────────────────────────────────────

interface PngImage {
  width: number;
  height: number;
  channels: number; // 3 (RGB) or 4 (RGBA)
  pixels: Uint8Array; // row-major RGBA/RGB pixel data
}

const PNG_SIGNATURE = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);

function decodePng(buf: Buffer): PngImage {
  // Verify signature
  if (buf.subarray(0, 8).compare(PNG_SIGNATURE) !== 0) {
    throw new Error("Not a valid PNG file");
  }

  let offset = 8;
  let width = 0;
  let height = 0;
  let bitDepth = 0;
  let colorType = 0;
  const idatChunks: Buffer[] = [];

  while (offset < buf.length) {
    const length = buf.readUInt32BE(offset);
    const type = buf.toString("ascii", offset + 4, offset + 8);
    const data = buf.subarray(offset + 8, offset + 8 + length);

    if (type === "IHDR") {
      width = data.readUInt32BE(0);
      height = data.readUInt32BE(4);
      bitDepth = data[8];
      colorType = data[9];
    } else if (type === "IDAT") {
      idatChunks.push(Buffer.from(data));
    } else if (type === "IEND") {
      break;
    }

    offset += 12 + length; // 4 (length) + 4 (type) + length + 4 (CRC)
  }

  if (width === 0 || height === 0) {
    throw new Error("Missing IHDR chunk");
  }
  if (bitDepth !== 8) {
    throw new Error(`Unsupported bit depth: ${bitDepth} (only 8-bit supported)`);
  }

  // Channels: colorType 2 = RGB (3), colorType 6 = RGBA (4)
  const channels = colorType === 6 ? 4 : colorType === 2 ? 3 : colorType === 0 ? 1 : 4;

  // Decompress all IDAT chunks
  const compressed = Buffer.concat(idatChunks);
  const raw = inflateSync(compressed);

  // Unfilter scanlines
  const stride = width * channels;
  const pixels = new Uint8Array(height * stride);

  for (let y = 0; y < height; y++) {
    const filterType = raw[y * (stride + 1)];
    const scanlineOffset = y * (stride + 1) + 1;
    const outOffset = y * stride;

    for (let x = 0; x < stride; x++) {
      const rawByte = raw[scanlineOffset + x];
      const a = x >= channels ? pixels[outOffset + x - channels] : 0; // left
      const b = y > 0 ? pixels[outOffset - stride + x] : 0; // above
      const c = x >= channels && y > 0 ? pixels[outOffset - stride + x - channels] : 0; // upper-left

      switch (filterType) {
        case 0: // None
          pixels[outOffset + x] = rawByte;
          break;
        case 1: // Sub
          pixels[outOffset + x] = (rawByte + a) & 0xff;
          break;
        case 2: // Up
          pixels[outOffset + x] = (rawByte + b) & 0xff;
          break;
        case 3: // Average
          pixels[outOffset + x] = (rawByte + ((a + b) >> 1)) & 0xff;
          break;
        case 4: // Paeth
          pixels[outOffset + x] = (rawByte + paethPredictor(a, b, c)) & 0xff;
          break;
        default:
          pixels[outOffset + x] = rawByte;
      }
    }
  }

  return { width, height, channels, pixels };
}

function paethPredictor(a: number, b: number, c: number): number {
  const p = a + b - c;
  const pa = Math.abs(p - a);
  const pb = Math.abs(p - b);
  const pc = Math.abs(p - c);
  if (pa <= pb && pa <= pc) return a;
  if (pb <= pc) return b;
  return c;
}

// ─── Pixel Comparison ────────────────────────────────────────────────────────

export interface FrameDiffResult {
  similarity: number;
  totalPixels: number;
  differentPixels: number;
  meanDelta: number;
  maxDelta: number;
  ssim: number;
  dimensionMatch: boolean;
}

/**
 * Compare two PNG files using decoded pixel data.
 * Returns per-pixel similarity, mean/max deltas, and a structural similarity (SSIM) estimate.
 */
export function compareFrames(pathA: string, pathB: string): FrameDiffResult {
  const bufA = readFileSync(pathA);
  const bufB = readFileSync(pathB);

  const imgA = decodePng(bufA);
  const imgB = decodePng(bufB);

  const dimensionMatch = imgA.width === imgB.width && imgA.height === imgB.height;

  if (!dimensionMatch) {
    return {
      similarity: 0,
      totalPixels: Math.max(imgA.width * imgA.height, imgB.width * imgB.height),
      differentPixels: Math.max(imgA.width * imgA.height, imgB.width * imgB.height),
      meanDelta: 255,
      maxDelta: 255,
      ssim: 0,
      dimensionMatch: false,
    };
  }

  const totalPixels = imgA.width * imgA.height;
  const channels = Math.min(imgA.channels, imgB.channels, 3); // Compare RGB only
  let differentPixels = 0;
  let deltaSum = 0;
  let maxDelta = 0;

  // Per-pixel comparison with tolerance
  const TOLERANCE = 2; // Allow minor rounding differences
  for (let i = 0; i < totalPixels; i++) {
    const offsetA = i * imgA.channels;
    const offsetB = i * imgB.channels;
    let pixelDelta = 0;

    for (let c = 0; c < channels; c++) {
      const diff = Math.abs(imgA.pixels[offsetA + c] - imgB.pixels[offsetB + c]);
      pixelDelta = Math.max(pixelDelta, diff);
    }

    if (pixelDelta > TOLERANCE) {
      differentPixels++;
    }
    deltaSum += pixelDelta;
    maxDelta = Math.max(maxDelta, pixelDelta);
  }

  const meanDelta = totalPixels > 0 ? deltaSum / totalPixels : 0;
  const similarity = totalPixels > 0
    ? Math.round(((totalPixels - differentPixels) / totalPixels) * 10000) / 100
    : 100;

  // Simplified SSIM: luminance-based structural similarity
  const ssim = computeSimplifiedSSIM(imgA, imgB);

  return {
    similarity,
    totalPixels,
    differentPixels,
    meanDelta: Math.round(meanDelta * 100) / 100,
    maxDelta,
    ssim: Math.round(ssim * 10000) / 10000,
    dimensionMatch,
  };
}

/**
 * Simplified SSIM computed over 8x8 blocks.
 * Uses luminance channel only (0.299R + 0.587G + 0.114B).
 */
function computeSimplifiedSSIM(a: PngImage, b: PngImage): number {
  const BLOCK = 8;
  const C1 = (0.01 * 255) ** 2;
  const C2 = (0.03 * 255) ** 2;

  const blocksX = Math.floor(a.width / BLOCK);
  const blocksY = Math.floor(a.height / BLOCK);
  if (blocksX === 0 || blocksY === 0) return 1.0;

  let ssimSum = 0;
  let blockCount = 0;

  for (let by = 0; by < blocksY; by++) {
    for (let bx = 0; bx < blocksX; bx++) {
      let sumA = 0, sumB = 0, sumA2 = 0, sumB2 = 0, sumAB = 0;
      const n = BLOCK * BLOCK;

      for (let dy = 0; dy < BLOCK; dy++) {
        for (let dx = 0; dx < BLOCK; dx++) {
          const px = bx * BLOCK + dx;
          const py = by * BLOCK + dy;
          const idx = (py * a.width + px);
          const offA = idx * a.channels;
          const offB = idx * b.channels;

          // Luminance
          const la = 0.299 * a.pixels[offA] + 0.587 * a.pixels[offA + 1] + 0.114 * a.pixels[offA + 2];
          const lb = 0.299 * b.pixels[offB] + 0.587 * b.pixels[offB + 1] + 0.114 * b.pixels[offB + 2];

          sumA += la;
          sumB += lb;
          sumA2 += la * la;
          sumB2 += lb * lb;
          sumAB += la * lb;
        }
      }

      const muA = sumA / n;
      const muB = sumB / n;
      const sigmaA2 = sumA2 / n - muA * muA;
      const sigmaB2 = sumB2 / n - muB * muB;
      const sigmaAB = sumAB / n - muA * muB;

      const ssim = ((2 * muA * muB + C1) * (2 * sigmaAB + C2)) /
                   ((muA * muA + muB * muB + C1) * (sigmaA2 + sigmaB2 + C2));

      ssimSum += ssim;
      blockCount++;
    }
  }

  return blockCount > 0 ? ssimSum / blockCount : 1.0;
}
