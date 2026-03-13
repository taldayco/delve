return Lerp(yf0, yf1, zs);
  }



  template <typename FNfloat>
  void DoSingleDomainWarp(int seed, float amp, float freq, FNfloat x, FNfloat y,
                          FNfloat &xr, FNfloat &yr) const {
    switch (mDomainWarpType) {
    case DomainWarpType_OpenSimplex2:
      SingleDomainWarpSimplexGradient(seed, amp * 38.283687591552734375f, freq,
                                      x, y, xr, yr, false);
      break;
    case DomainWarpType_OpenSimplex2Reduced:
      SingleDomainWarpSimplexGradient(seed, amp * 16.0f, freq, x, y, xr, yr,
                                      true);
      break;
    case DomainWarpType_BasicGrid:
      SingleDomainWarpBasicGrid(seed, amp, freq, x, y, xr, yr);
      break;
    }
  }

  template <typename FNfloat>
  void DoSingleDomainWarp(int seed, float amp, float freq, FNfloat x, FNfloat y,
                          FNfloat z, FNfloat &xr, FNfloat &yr,
                          FNfloat &zr) const {
    switch (mDomainWarpType) {
    case DomainWarpType_OpenSimplex2:
      SingleDomainWarpOpenSimplex2Gradient(seed, amp * 32.69428253173828125f,
                                           freq, x, y, z, xr, yr, zr, false);
      break;
    case DomainWarpType_OpenSimplex2Reduced:
      SingleDomainWarpOpenSimplex2Gradient(seed, amp * 7.71604938271605f, freq,
                                           x, y, z, xr, yr, zr, true);
      break;
    case DomainWarpType_BasicGrid:
      SingleDomainWarpBasicGrid(seed, amp, freq, x, y, z, xr, yr, zr);
      break;
    }
  }



  template <typename FNfloat>
  void DomainWarpSingle(FNfloat &x, FNfloat &y) const {
    int seed = mSeed;
    float amp = mDomainWarpAmp * mFractalBounding;
    float freq = mFrequency;

    FNfloat xs = x;
    FNfloat ys = y;
    TransformDomainWarpCoordinate(xs, ys);

    DoSingleDomainWarp(seed, amp, freq, xs, ys, x, y);
  }

  template <typename FNfloat>
  void DomainWarpSingle(FNfloat &x, FNfloat &y, FNfloat &z) const {
    int seed = mSeed;
    float amp = mDomainWarpAmp * mFractalBounding;
    float freq = mFrequency;

    FNfloat xs = x;
    FNfloat ys = y;
    FNfloat zs = z;
    TransformDomainWarpCoordinate(xs, ys, zs);

    DoSingleDomainWarp(seed, amp, freq, xs, ys, zs, x, y, z);
  }



  template <typename FNfloat>
  void DomainWarpFractalProgressive(FNfloat &x, FNfloat &y) const {
    int seed = mSeed;
    float amp = mDomainWarpAmp * mFractalBounding;
    float freq = mFrequency;

    for (int i = 0; i < mOctaves; i++) {
      FNfloat xs = x;
      FNfloat ys = y;
      TransformDomainWarpCoordinate(xs, ys);

      DoSingleDomainWarp(seed, amp, freq, xs, ys, x, y);

      seed++;
      amp *= mGain;
      freq *= mLacunarity;
    }
  }

  template <typename FNfloat>
  void DomainWarpFractalProgressive(FNfloat &x, FNfloat &y, FNfloat &z) const {
    int seed = mSeed;
    float amp = mDomainWarpAmp * mFractalBounding;
    float freq = mFrequency;

    for (int i = 0; i < mOctaves; i++) {
      FNfloat xs = x;
      FNfloat ys = y;
      FNfloat zs = z;
      TransformDomainWarpCoordinate(xs, ys, zs);

      DoSingleDomainWarp(seed, amp, freq, xs, ys, zs, x, y, z);

      seed++;
      amp *= mGain;
      freq *= mLacunarity;
    }
  }



  template <typename FNfloat>
  void DomainWarpFractalIndependent(FNfloat &x, FNfloat &y) const {
    FNfloat xs = x;
    FNfloat ys = y;
    TransformDomainWarpCoordinate(xs, ys);

    int seed = mSeed;
    float amp = mDomainWarpAmp * mFractalBounding;
    float freq = mFrequency;

    for (int i = 0; i < mOctaves; i++) {
      DoSingleDomainWarp(seed, amp, freq, xs, ys, x, y);

      seed++;
      amp *= mGain;
      freq *= mLacunarity;
    }
  }

  template <typename FNfloat>
  void DomainWarpFractalIndependent(FNfloat &x, FNfloat &y, FNfloat &z) const {
    FNfloat xs = x;
    FNfloat ys = y;
    FNfloat zs = z;
    TransformDomainWarpCoordinate(xs, ys, zs);

    int seed = mSeed;
    float amp = mDomainWarpAmp * mFractalBounding;
    float freq = mFrequency;

    for (int i = 0; i < mOctaves; i++) {
      DoSingleDomainWarp(seed, amp, freq, xs, ys, zs, x, y, z);

      seed++;
      amp *= mGain;
      freq *= mLacunarity;
    }
  }



  template <typename FNfloat>
  void SingleDomainWarpBasicGrid(int seed, float warpAmp, float frequency,
                                 FNfloat x, FNfloat y, FNfloat &xr,
                                 FNfloat &yr) const {
    FNfloat xf = x * frequency;
    FNfloat yf = y * frequency;

    int x0 = FastFloor(xf);
    int y0 = FastFloor(yf);

    float xs = InterpHermite((float)(xf - x0));
    float ys = InterpHermite((float)(yf - y0));

    x0 *= PrimeX;
    y0 *= PrimeY;
    int x1 = x0 + PrimeX;
    int y1 = y0 + PrimeY;

    int hash0 = Hash(seed, x0, y0) & (255 << 1);
    int hash1 = Hash(seed, x1, y0) & (255 << 1);