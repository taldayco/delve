template <typename FNfloat>
  float SingleValueCubic(int seed, FNfloat x, FNfloat y, FNfloat z) const {
    int x1 = FastFloor(x);
    int y1 = FastFloor(y);
    int z1 = FastFloor(z);

    float xs = (float)(x - x1);
    float ys = (float)(y - y1);
    float zs = (float)(z - z1);

    x1 *= PrimeX;
    y1 *= PrimeY;
    z1 *= PrimeZ;

    int x0 = x1 - PrimeX;
    int y0 = y1 - PrimeY;
    int z0 = z1 - PrimeZ;
    int x2 = x1 + PrimeX;
    int y2 = y1 + PrimeY;
    int z2 = z1 + PrimeZ;
    int x3 = x1 + (int)((long)PrimeX << 1);
    int y3 = y1 + (int)((long)PrimeY << 1);
    int z3 = z1 + (int)((long)PrimeZ << 1);

    return CubicLerp(CubicLerp(CubicLerp(ValCoord(seed, x0, y0, z0),
                                         ValCoord(seed, x1, y0, z0),
                                         ValCoord(seed, x2, y0, z0),
                                         ValCoord(seed, x3, y0, z0), xs),
                               CubicLerp(ValCoord(seed, x0, y1, z0),
                                         ValCoord(seed, x1, y1, z0),
                                         ValCoord(seed, x2, y1, z0),
                                         ValCoord(seed, x3, y1, z0), xs),
                               CubicLerp(ValCoord(seed, x0, y2, z0),
                                         ValCoord(seed, x1, y2, z0),
                                         ValCoord(seed, x2, y2, z0),
                                         ValCoord(seed, x3, y2, z0), xs),
                               CubicLerp(ValCoord(seed, x0, y3, z0),
                                         ValCoord(seed, x1, y3, z0),
                                         ValCoord(seed, x2, y3, z0),
                                         ValCoord(seed, x3, y3, z0), xs),
                               ys),
                     CubicLerp(CubicLerp(ValCoord(seed, x0, y0, z1),
                                         ValCoord(seed, x1, y0, z1),
                                         ValCoord(seed, x2, y0, z1),
                                         ValCoord(seed, x3, y0, z1), xs),
                               CubicLerp(ValCoord(seed, x0, y1, z1),
                                         ValCoord(seed, x1, y1, z1),
                                         ValCoord(seed, x2, y1, z1),
                                         ValCoord(seed, x3, y1, z1), xs),
                               CubicLerp(ValCoord(seed, x0, y2, z1),
                                         ValCoord(seed, x1, y2, z1),
                                         ValCoord(seed, x2, y2, z1),
                                         ValCoord(seed, x3, y2, z1), xs),
                               CubicLerp(ValCoord(seed, x0, y3, z1),
                                         ValCoord(seed, x1, y3, z1),
                                         ValCoord(seed, x2, y3, z1),
                                         ValCoord(seed, x3, y3, z1), xs),
                               ys),
                     CubicLerp(CubicLerp(ValCoord(seed, x0, y0, z2),
                                         ValCoord(seed, x1, y0, z2),
                                         ValCoord(seed, x2, y0, z2),
                                         ValCoord(seed, x3, y0, z2), xs),
                               CubicLerp(ValCoord(seed, x0, y1, z2),
                                         ValCoord(seed, x1, y1, z2),
                                         ValCoord(seed, x2, y1, z2),
                                         ValCoord(seed, x3, y1, z2), xs),
                               CubicLerp(ValCoord(seed, x0, y2, z2),
                                         ValCoord(seed, x1, y2, z2),
                                         ValCoord(seed, x2, y2, z2),
                                         ValCoord(seed, x3, y2, z2), xs),
                               CubicLerp(ValCoord(seed, x0, y3, z2),
                                         ValCoord(seed, x1, y3, z2),
                                         ValCoord(seed, x2, y3, z2),
                                         ValCoord(seed, x3, y3, z2), xs),
                               ys),
                     CubicLerp(CubicLerp(ValCoord(seed, x0, y0, z3),
                                         ValCoord(seed, x1, y0, z3),
                                         ValCoord(seed, x2, y0, z3),
                                         ValCoord(seed, x3, y0, z3), xs),
                               CubicLerp(ValCoord(seed, x0, y1, z3),
                                         ValCoord(seed, x1, y1, z3),
                                         ValCoord(seed, x2, y1, z3),
                                         ValCoord(seed, x3, y1, z3), xs),
                               CubicLerp(ValCoord(seed, x0, y2, z3),
                                         ValCoord(seed, x1, y2, z3),
                                         ValCoord(seed, x2, y2, z3),
                                         ValCoord(seed, x3, y2, z3), xs),
                               CubicLerp(ValCoord(seed, x0, y3, z3),
                                         ValCoord(seed, x1, y3, z3),
                                         ValCoord(seed, x2, y3, z3),
                                         ValCoord(seed, x3, y3, z3), xs),
                               ys),
                     zs) *
           (1 / (1.5f * 1.5f * 1.5f));
  }



  template <typename FNfloat>
  float SingleValue(int seed, FNfloat x, FNfloat y) const {
    int x0 = FastFloor(x);
    int y0 = FastFloor(y);

    float xs = InterpHermite((float)(x - x0));
    float ys = InterpHermite((float)(y - y0));

    x0 *= PrimeX;
    y0 *= PrimeY;
    int x1 = x0 + PrimeX;
    int y1 = y0 + PrimeY;

    float xf0 = Lerp(ValCoord(seed, x0, y0), ValCoord(seed, x1, y0), xs);
    float xf1 = Lerp(ValCoord(seed, x0, y1), ValCoord(seed, x1, y1), xs);

    return Lerp(xf0, xf1, ys);
  }

  template <typename FNfloat>
  float SingleValue(int seed, FNfloat x, FNfloat y, FNfloat z) const {
    int x0 = FastFloor(x);
    int y0 = FastFloor(y);
    int z0 = FastFloor(z);

    float xs = InterpHermite((float)(x - x0));
    float ys = InterpHermite((float)(y - y0));
    float zs = InterpHermite((float)(z - z0));

    x0 *= PrimeX;
    y0 *= PrimeY;
    z0 *= PrimeZ;
    int x1 = x0 + PrimeX;
    int y1 = y0 + PrimeY;
    int z1 = z0 + PrimeZ;

    float xf00 =
        Lerp(ValCoord(seed, x0, y0, z0), ValCoord(seed, x1, y0, z0), xs);
    float xf10 =
        Lerp(ValCoord(seed, x0, y1, z0), ValCoord(seed, x1, y1, z0), xs);
    float xf01 =
        Lerp(ValCoord(seed, x0, y0, z1), ValCoord(seed, x1, y0, z1), xs);
    float xf11 =
        Lerp(ValCoord(seed, x0, y1, z1), ValCoord(seed, x1, y1, z1), xs);

    float yf0 = Lerp(xf00, xf10, ys);
    float yf1 = Lerp(xf01, xf11, ys);