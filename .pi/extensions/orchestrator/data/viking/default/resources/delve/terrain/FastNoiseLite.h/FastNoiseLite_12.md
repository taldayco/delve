float lx0x = Lerp(Lookup<float>::RandVecs2D[hash0],
                      Lookup<float>::RandVecs2D[hash1], xs);
    float ly0x = Lerp(Lookup<float>::RandVecs2D[hash0 | 1],
                      Lookup<float>::RandVecs2D[hash1 | 1], xs);

    hash0 = Hash(seed, x0, y1) & (255 << 1);
    hash1 = Hash(seed, x1, y1) & (255 << 1);

    float lx1x = Lerp(Lookup<float>::RandVecs2D[hash0],
                      Lookup<float>::RandVecs2D[hash1], xs);
    float ly1x = Lerp(Lookup<float>::RandVecs2D[hash0 | 1],
                      Lookup<float>::RandVecs2D[hash1 | 1], xs);

    xr += Lerp(lx0x, lx1x, ys) * warpAmp;
    yr += Lerp(ly0x, ly1x, ys) * warpAmp;
  }

  template <typename FNfloat>
  void SingleDomainWarpBasicGrid(int seed, float warpAmp, float frequency,
                                 FNfloat x, FNfloat y, FNfloat z, FNfloat &xr,
                                 FNfloat &yr, FNfloat &zr) const {
    FNfloat xf = x * frequency;
    FNfloat yf = y * frequency;
    FNfloat zf = z * frequency;

    int x0 = FastFloor(xf);
    int y0 = FastFloor(yf);
    int z0 = FastFloor(zf);

    float xs = InterpHermite((float)(xf - x0));
    float ys = InterpHermite((float)(yf - y0));
    float zs = InterpHermite((float)(zf - z0));

    x0 *= PrimeX;
    y0 *= PrimeY;
    z0 *= PrimeZ;
    int x1 = x0 + PrimeX;
    int y1 = y0 + PrimeY;
    int z1 = z0 + PrimeZ;

    int hash0 = Hash(seed, x0, y0, z0) & (255 << 2);
    int hash1 = Hash(seed, x1, y0, z0) & (255 << 2);

    float lx0x = Lerp(Lookup<float>::RandVecs3D[hash0],
                      Lookup<float>::RandVecs3D[hash1], xs);
    float ly0x = Lerp(Lookup<float>::RandVecs3D[hash0 | 1],
                      Lookup<float>::RandVecs3D[hash1 | 1], xs);
    float lz0x = Lerp(Lookup<float>::RandVecs3D[hash0 | 2],
                      Lookup<float>::RandVecs3D[hash1 | 2], xs);

    hash0 = Hash(seed, x0, y1, z0) & (255 << 2);
    hash1 = Hash(seed, x1, y1, z0) & (255 << 2);

    float lx1x = Lerp(Lookup<float>::RandVecs3D[hash0],
                      Lookup<float>::RandVecs3D[hash1], xs);
    float ly1x = Lerp(Lookup<float>::RandVecs3D[hash0 | 1],
                      Lookup<float>::RandVecs3D[hash1 | 1], xs);
    float lz1x = Lerp(Lookup<float>::RandVecs3D[hash0 | 2],
                      Lookup<float>::RandVecs3D[hash1 | 2], xs);

    float lx0y = Lerp(lx0x, lx1x, ys);
    float ly0y = Lerp(ly0x, ly1x, ys);
    float lz0y = Lerp(lz0x, lz1x, ys);

    hash0 = Hash(seed, x0, y0, z1) & (255 << 2);
    hash1 = Hash(seed, x1, y0, z1) & (255 << 2);

    lx0x = Lerp(Lookup<float>::RandVecs3D[hash0],
                Lookup<float>::RandVecs3D[hash1], xs);
    ly0x = Lerp(Lookup<float>::RandVecs3D[hash0 | 1],
                Lookup<float>::RandVecs3D[hash1 | 1], xs);
    lz0x = Lerp(Lookup<float>::RandVecs3D[hash0 | 2],
                Lookup<float>::RandVecs3D[hash1 | 2], xs);

    hash0 = Hash(seed, x0, y1, z1) & (255 << 2);
    hash1 = Hash(seed, x1, y1, z1) & (255 << 2);

    lx1x = Lerp(Lookup<float>::RandVecs3D[hash0],
                Lookup<float>::RandVecs3D[hash1], xs);
    ly1x = Lerp(Lookup<float>::RandVecs3D[hash0 | 1],
                Lookup<float>::RandVecs3D[hash1 | 1], xs);
    lz1x = Lerp(Lookup<float>::RandVecs3D[hash0 | 2],
                Lookup<float>::RandVecs3D[hash1 | 2], xs);

    xr += Lerp(lx0y, Lerp(lx0x, lx1x, ys), zs) * warpAmp;
    yr += Lerp(ly0y, Lerp(ly0x, ly1x, ys), zs) * warpAmp;
    zr += Lerp(lz0y, Lerp(lz0x, lz1x, ys), zs) * warpAmp;
  }



  template <typename FNfloat>
  void SingleDomainWarpSimplexGradient(int seed, float warpAmp, float frequency,
                                       FNfloat x, FNfloat y, FNfloat &xr,
                                       FNfloat &yr, bool outGradOnly) const {
    const float SQRT3 = 1.7320508075688772935274463415059f;
    const float G2 = (3 - SQRT3) / 6;

    x *= frequency;
    y *= frequency;



    int i = FastFloor(x);
    int j = FastFloor(y);
    float xi = (float)(x - i);
    float yi = (float)(y - j);

    float t = (xi + yi) * G2;
    float x0 = (float)(xi - t);
    float y0 = (float)(yi - t);

    i *= PrimeX;
    j *= PrimeY;

    float vx, vy;
    vx = vy = 0;

    float a = 0.5f - x0 * x0 - y0 * y0;
    if (a > 0) {
      float aaaa = (a * a) * (a * a);
      float xo, yo;
      if (outGradOnly)
        GradCoordOut(seed, i, j, xo, yo);
      else
        GradCoordDual(seed, i, j, x0, y0, xo, yo);
      vx += aaaa * xo;
      vy += aaaa * yo;
    }