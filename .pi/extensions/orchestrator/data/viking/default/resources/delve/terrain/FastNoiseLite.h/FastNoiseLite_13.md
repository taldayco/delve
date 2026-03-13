float c = (float)(2 * (1 - 2 * G2) * (1 / G2 - 2)) * t +
              ((float)(-2 * (1 - 2 * G2) * (1 - 2 * G2)) + a);
    if (c > 0) {
      float x2 = x0 + (2 * (float)G2 - 1);
      float y2 = y0 + (2 * (float)G2 - 1);
      float cccc = (c * c) * (c * c);
      float xo, yo;
      if (outGradOnly)
        GradCoordOut(seed, i + PrimeX, j + PrimeY, xo, yo);
      else
        GradCoordDual(seed, i + PrimeX, j + PrimeY, x2, y2, xo, yo);
      vx += cccc * xo;
      vy += cccc * yo;
    }

    if (y0 > x0) {
      float x1 = x0 + (float)G2;
      float y1 = y0 + ((float)G2 - 1);
      float b = 0.5f - x1 * x1 - y1 * y1;
      if (b > 0) {
        float bbbb = (b * b) * (b * b);
        float xo, yo;
        if (outGradOnly)
          GradCoordOut(seed, i, j + PrimeY, xo, yo);
        else
          GradCoordDual(seed, i, j + PrimeY, x1, y1, xo, yo);
        vx += bbbb * xo;
        vy += bbbb * yo;
      }
    } else {
      float x1 = x0 + ((float)G2 - 1);
      float y1 = y0 + (float)G2;
      float b = 0.5f - x1 * x1 - y1 * y1;
      if (b > 0) {
        float bbbb = (b * b) * (b * b);
        float xo, yo;
        if (outGradOnly)
          GradCoordOut(seed, i + PrimeX, j, xo, yo);
        else
          GradCoordDual(seed, i + PrimeX, j, x1, y1, xo, yo);
        vx += bbbb * xo;
        vy += bbbb * yo;
      }
    }

    xr += vx * warpAmp;
    yr += vy * warpAmp;
  }

  template <typename FNfloat>
  void SingleDomainWarpOpenSimplex2Gradient(int seed, float warpAmp,
                                            float frequency, FNfloat x,
                                            FNfloat y, FNfloat z, FNfloat &xr,
                                            FNfloat &yr, FNfloat &zr,
                                            bool outGradOnly) const {
    x *= frequency;
    y *= frequency;
    z *= frequency;



    int i = FastRound(x);
    int j = FastRound(y);
    int k = FastRound(z);
    float x0 = (float)x - i;
    float y0 = (float)y - j;
    float z0 = (float)z - k;

    int xNSign = (int)(-x0 - 1.0f) | 1;
    int yNSign = (int)(-y0 - 1.0f) | 1;
    int zNSign = (int)(-z0 - 1.0f) | 1;

    float ax0 = xNSign * -x0;
    float ay0 = yNSign * -y0;
    float az0 = zNSign * -z0;

    i *= PrimeX;
    j *= PrimeY;
    k *= PrimeZ;

    float vx, vy, vz;
    vx = vy = vz = 0;

    float a = (0.6f - x0 * x0) - (y0 * y0 + z0 * z0);
    for (int l = 0; l < 2; l++) {
      if (a > 0) {
        float aaaa = (a * a) * (a * a);
        float xo, yo, zo;
        if (outGradOnly)
          GradCoordOut(seed, i, j, k, xo, yo, zo);
        else
          GradCoordDual(seed, i, j, k, x0, y0, z0, xo, yo, zo);
        vx += aaaa * xo;
        vy += aaaa * yo;
        vz += aaaa * zo;
      }

      float b = a + 1;
      int i1 = i;
      int j1 = j;
      int k1 = k;
      float x1 = x0;
      float y1 = y0;
      float z1 = z0;

      if (ax0 >= ay0 && ax0 >= az0) {
        x1 += xNSign;
        b -= xNSign * 2 * x1;
        i1 -= xNSign * PrimeX;
      } else if (ay0 > ax0 && ay0 >= az0) {
        y1 += yNSign;
        b -= yNSign * 2 * y1;
        j1 -= yNSign * PrimeY;
      } else {
        z1 += zNSign;
        b -= zNSign * 2 * z1;
        k1 -= zNSign * PrimeZ;
      }

      if (b > 0) {
        float bbbb = (b * b) * (b * b);
        float xo, yo, zo;
        if (outGradOnly)
          GradCoordOut(seed, i1, j1, k1, xo, yo, zo);
        else
          GradCoordDual(seed, i1, j1, k1, x1, y1, z1, xo, yo, zo);
        vx += bbbb * xo;
        vy += bbbb * yo;
        vz += bbbb * zo;
      }

      if (l == 1)
        break;

      ax0 = 0.5f - ax0;
      ay0 = 0.5f - ay0;
      az0 = 0.5f - az0;

      x0 = xNSign * ax0;
      y0 = yNSign * ay0;
      z0 = zNSign * az0;

      a += (0.75f - ax0) - (ay0 + az0);

      i += (xNSign >> 1) & PrimeX;
      j += (yNSign >> 1) & PrimeY;
      k += (zNSign >> 1) & PrimeZ;

      xNSign = -xNSign;
      yNSign = -yNSign;
      zNSign = -zNSign;

      seed += 1293373;
    }

    xr += vx * warpAmp;
    yr += vy * warpAmp;
    zr += vz * warpAmp;
  }
};

template <>
struct FastNoiseLite::Arguments_must_be_floating_point_values<float> {};
template <>
struct FastNoiseLite::Arguments_must_be_floating_point_values<double> {};
template <>
struct FastNoiseLite::Arguments_must_be_floating_point_values<long double> {};