template <typename FNfloat>
  float GenFractalRidged(FNfloat x, FNfloat y, FNfloat z) const {
    int seed = mSeed;
    float sum = 0;
    float amp = mFractalBounding;

    for (int i = 0; i < mOctaves; i++) {
      float noise = FastAbs(GenNoiseSingle(seed++, x, y, z));
      sum += (noise * -2 + 1) * amp;
      amp *= Lerp(1.0f, 1 - noise, mWeightedStrength);

      x *= mLacunarity;
      y *= mLacunarity;
      z *= mLacunarity;
      amp *= mGain;
    }

    return sum;
  }



  template <typename FNfloat>
  float GenFractalPingPong(FNfloat x, FNfloat y) const {
    int seed = mSeed;
    float sum = 0;
    float amp = mFractalBounding;

    for (int i = 0; i < mOctaves; i++) {
      float noise =
          PingPong((GenNoiseSingle(seed++, x, y) + 1) * mPingPongStrength);
      sum += (noise - 0.5f) * 2 * amp;
      amp *= Lerp(1.0f, noise, mWeightedStrength);

      x *= mLacunarity;
      y *= mLacunarity;
      amp *= mGain;
    }

    return sum;
  }

  template <typename FNfloat>
  float GenFractalPingPong(FNfloat x, FNfloat y, FNfloat z) const {
    int seed = mSeed;
    float sum = 0;
    float amp = mFractalBounding;

    for (int i = 0; i < mOctaves; i++) {
      float noise =
          PingPong((GenNoiseSingle(seed++, x, y, z) + 1) * mPingPongStrength);
      sum += (noise - 0.5f) * 2 * amp;
      amp *= Lerp(1.0f, noise, mWeightedStrength);

      x *= mLacunarity;
      y *= mLacunarity;
      z *= mLacunarity;
      amp *= mGain;
    }

    return sum;
  }



  template <typename FNfloat>
  float SingleSimplex(int seed, FNfloat x, FNfloat y) const {


    const float SQRT3 = 1.7320508075688772935274463415059f;
    const float G2 = (3 - SQRT3) / 6;



    int i = FastFloor(x);
    int j = FastFloor(y);
    float xi = (float)(x - i);
    float yi = (float)(y - j);

    float t = (xi + yi) * G2;
    float x0 = (float)(xi - t);
    float y0 = (float)(yi - t);

    i *= PrimeX;
    j *= PrimeY;

    float n0, n1, n2;

    float a = 0.5f - x0 * x0 - y0 * y0;
    if (a <= 0)
      n0 = 0;
    else {
      n0 = (a * a) * (a * a) * GradCoord(seed, i, j, x0, y0);
    }

    float c = (float)(2 * (1 - 2 * G2) * (1 / G2 - 2)) * t +
              ((float)(-2 * (1 - 2 * G2) * (1 - 2 * G2)) + a);
    if (c <= 0)
      n2 = 0;
    else {
      float x2 = x0 + (2 * (float)G2 - 1);
      float y2 = y0 + (2 * (float)G2 - 1);
      n2 = (c * c) * (c * c) * GradCoord(seed, i + PrimeX, j + PrimeY, x2, y2);
    }

    if (y0 > x0) {
      float x1 = x0 + (float)G2;
      float y1 = y0 + ((float)G2 - 1);
      float b = 0.5f - x1 * x1 - y1 * y1;
      if (b <= 0)
        n1 = 0;
      else {
        n1 = (b * b) * (b * b) * GradCoord(seed, i, j + PrimeY, x1, y1);
      }
    } else {
      float x1 = x0 + ((float)G2 - 1);
      float y1 = y0 + (float)G2;
      float b = 0.5f - x1 * x1 - y1 * y1;
      if (b <= 0)
        n1 = 0;
      else {
        n1 = (b * b) * (b * b) * GradCoord(seed, i + PrimeX, j, x1, y1);
      }
    }

    return (n0 + n1 + n2) * 99.83685446303647f;
  }

  template <typename FNfloat>
  float SingleOpenSimplex2(int seed, FNfloat x, FNfloat y, FNfloat z) const {




    int i = FastRound(x);
    int j = FastRound(y);
    int k = FastRound(z);
    float x0 = (float)(x - i);
    float y0 = (float)(y - j);
    float z0 = (float)(z - k);

    int xNSign = (int)(-1.0f - x0) | 1;
    int yNSign = (int)(-1.0f - y0) | 1;
    int zNSign = (int)(-1.0f - z0) | 1;

    float ax0 = xNSign * -x0;
    float ay0 = yNSign * -y0;
    float az0 = zNSign * -z0;

    i *= PrimeX;
    j *= PrimeY;
    k *= PrimeZ;

    float value = 0;
    float a = (0.6f - x0 * x0) - (y0 * y0 + z0 * z0);

    for (int l = 0;; l++) {
      if (a > 0) {
        value += (a * a) * (a * a) * GradCoord(seed, i, j, k, x0, y0, z0);
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
        value += (b * b) * (b * b) * GradCoord(seed, i1, j1, k1, x1, y1, z1);
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

      seed = ~seed;
    }

    return value * 32.69428253173828125f;
  }



  template <typename FNfloat>
  float SingleOpenSimplex2S(int seed, FNfloat x, FNfloat y) const {


    const FNfloat SQRT3 = (FNfloat)1.7320508075688772935274463415059;
    const FNfloat G2 = (3 - SQRT3) / 6;



    int i = FastFloor(x);
    int j = FastFloor(y);
    float xi = (float)(x - i);
    float yi = (float)(y - j);

    i *= PrimeX;
    j *= PrimeY;
    int i1 = i + PrimeX;
    int j1 = j + PrimeY;