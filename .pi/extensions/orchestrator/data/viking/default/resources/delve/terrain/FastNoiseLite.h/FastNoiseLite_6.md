float t = (xi + yi) * (float)G2;
    float x0 = xi - t;
    float y0 = yi - t;

    float a0 = (2.0f / 3.0f) - x0 * x0 - y0 * y0;
    float value = (a0 * a0) * (a0 * a0) * GradCoord(seed, i, j, x0, y0);

    float a1 = (float)(2 * (1 - 2 * G2) * (1 / G2 - 2)) * t +
               ((float)(-2 * (1 - 2 * G2) * (1 - 2 * G2)) + a0);
    float x1 = x0 - (float)(1 - 2 * G2);
    float y1 = y0 - (float)(1 - 2 * G2);
    value += (a1 * a1) * (a1 * a1) * GradCoord(seed, i1, j1, x1, y1);


    float xmyi = xi - yi;
    if (t > G2) {
      if (xi + xmyi > 1) {
        float x2 = x0 + (float)(3 * G2 - 2);
        float y2 = y0 + (float)(3 * G2 - 1);
        float a2 = (2.0f / 3.0f) - x2 * x2 - y2 * y2;
        if (a2 > 0) {
          value += (a2 * a2) * (a2 * a2) *
                   GradCoord(seed, i + (PrimeX << 1), j + PrimeY, x2, y2);
        }
      } else {
        float x2 = x0 + (float)G2;
        float y2 = y0 + (float)(G2 - 1);
        float a2 = (2.0f / 3.0f) - x2 * x2 - y2 * y2;
        if (a2 > 0) {
          value +=
              (a2 * a2) * (a2 * a2) * GradCoord(seed, i, j + PrimeY, x2, y2);
        }
      }

      if (yi - xmyi > 1) {
        float x3 = x0 + (float)(3 * G2 - 1);
        float y3 = y0 + (float)(3 * G2 - 2);
        float a3 = (2.0f / 3.0f) - x3 * x3 - y3 * y3;
        if (a3 > 0) {
          value += (a3 * a3) * (a3 * a3) *
                   GradCoord(seed, i + PrimeX, j + (PrimeY << 1), x3, y3);
        }
      } else {
        float x3 = x0 + (float)(G2 - 1);
        float y3 = y0 + (float)G2;
        float a3 = (2.0f / 3.0f) - x3 * x3 - y3 * y3;
        if (a3 > 0) {
          value +=
              (a3 * a3) * (a3 * a3) * GradCoord(seed, i + PrimeX, j, x3, y3);
        }
      }
    } else {
      if (xi + xmyi < 0) {
        float x2 = x0 + (float)(1 - G2);
        float y2 = y0 - (float)G2;
        float a2 = (2.0f / 3.0f) - x2 * x2 - y2 * y2;
        if (a2 > 0) {
          value +=
              (a2 * a2) * (a2 * a2) * GradCoord(seed, i - PrimeX, j, x2, y2);
        }
      } else {
        float x2 = x0 + (float)(G2 - 1);
        float y2 = y0 + (float)G2;
        float a2 = (2.0f / 3.0f) - x2 * x2 - y2 * y2;
        if (a2 > 0) {
          value +=
              (a2 * a2) * (a2 * a2) * GradCoord(seed, i + PrimeX, j, x2, y2);
        }
      }

      if (yi < xmyi) {
        float x2 = x0 - (float)G2;
        float y2 = y0 - (float)(G2 - 1);
        float a2 = (2.0f / 3.0f) - x2 * x2 - y2 * y2;
        if (a2 > 0) {
          value +=
              (a2 * a2) * (a2 * a2) * GradCoord(seed, i, j - PrimeY, x2, y2);
        }
      } else {
        float x2 = x0 + (float)G2;
        float y2 = y0 + (float)(G2 - 1);
        float a2 = (2.0f / 3.0f) - x2 * x2 - y2 * y2;
        if (a2 > 0) {
          value +=
              (a2 * a2) * (a2 * a2) * GradCoord(seed, i, j + PrimeY, x2, y2);
        }
      }
    }

    return value * 18.24196194486065f;
  }

  template <typename FNfloat>
  float SingleOpenSimplex2S(int seed, FNfloat x, FNfloat y, FNfloat z) const {




    int i = FastFloor(x);
    int j = FastFloor(y);
    int k = FastFloor(z);
    float xi = (float)(x - i);
    float yi = (float)(y - j);
    float zi = (float)(z - k);

    i *= PrimeX;
    j *= PrimeY;
    k *= PrimeZ;
    int seed2 = seed + 1293373;

    int xNMask = (int)(-0.5f - xi);
    int yNMask = (int)(-0.5f - yi);
    int zNMask = (int)(-0.5f - zi);

    float x0 = xi + xNMask;
    float y0 = yi + yNMask;
    float z0 = zi + zNMask;
    float a0 = 0.75f - x0 * x0 - y0 * y0 - z0 * z0;
    float value = (a0 * a0) * (a0 * a0) *
                  GradCoord(seed, i + (xNMask & PrimeX), j + (yNMask & PrimeY),
                            k + (zNMask & PrimeZ), x0, y0, z0);

    float x1 = xi - 0.5f;
    float y1 = yi - 0.5f;
    float z1 = zi - 0.5f;
    float a1 = 0.75f - x1 * x1 - y1 * y1 - z1 * z1;
    value += (a1 * a1) * (a1 * a1) *
             GradCoord(seed2, i + PrimeX, j + PrimeY, k + PrimeZ, x1, y1, z1);

    float xAFlipMask0 = ((xNMask | 1) << 1) * x1;
    float yAFlipMask0 = ((yNMask | 1) << 1) * y1;
    float zAFlipMask0 = ((zNMask | 1) << 1) * z1;
    float xAFlipMask1 = (-2 - (xNMask << 2)) * x1 - 1.0f;
    float yAFlipMask1 = (-2 - (yNMask << 2)) * y1 - 1.0f;
    float zAFlipMask1 = (-2 - (zNMask << 2)) * z1 - 1.0f;

    bool skip5 = false;
    float a2 = xAFlipMask0 + a0;
    if (a2 > 0) {
      float x2 = x0 - (xNMask | 1);
      float y2 = y0;
      float z2 = z0;
      value += (a2 * a2) * (a2 * a2) *
               GradCoord(seed, i + (~xNMask & PrimeX), j + (yNMask & PrimeY),
                         k + (zNMask & PrimeZ), x2, y2, z2);
    } else {
      float a3 = yAFlipMask0 + zAFlipMask0 + a0;
      if (a3 > 0) {
        float x3 = x0;
        float y3 = y0 - (yNMask | 1);
        float z3 = z0 - (zNMask | 1);
        value += (a3 * a3) * (a3 * a3) *
                 GradCoord(seed, i + (xNMask & PrimeX), j + (~yNMask & PrimeY),
                           k + (~zNMask & PrimeZ), x3, y3, z3);
      }

      float a4 = xAFlipMask1 + a1;
      if (a4 > 0) {
        float x4 = (xNMask | 1) + x1;
        float y4 = y1;
        float z4 = z1;
        value += (a4 * a4) * (a4 * a4) *
                 GradCoord(seed2, i + (xNMask & (PrimeX * 2)), j + PrimeY,
                           k + PrimeZ, x4, y4, z4);
        skip5 = true;
      }
    }