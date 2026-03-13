float xg = Lookup<float>::Gradients2D[hash];
    float yg = Lookup<float>::Gradients2D[hash | 1];

    return xd * xg + yd * yg;
  }

  float GradCoord(int seed, int xPrimed, int yPrimed, int zPrimed, float xd,
                  float yd, float zd) const {
    int hash = Hash(seed, xPrimed, yPrimed, zPrimed);
    hash ^= hash >> 15;
    hash &= 63 << 2;

    float xg = Lookup<float>::Gradients3D[hash];
    float yg = Lookup<float>::Gradients3D[hash | 1];
    float zg = Lookup<float>::Gradients3D[hash | 2];

    return xd * xg + yd * yg + zd * zg;
  }

  void GradCoordOut(int seed, int xPrimed, int yPrimed, float &xo,
                    float &yo) const {
    int hash = Hash(seed, xPrimed, yPrimed) & (255 << 1);

    xo = Lookup<float>::RandVecs2D[hash];
    yo = Lookup<float>::RandVecs2D[hash | 1];
  }

  void GradCoordOut(int seed, int xPrimed, int yPrimed, int zPrimed, float &xo,
                    float &yo, float &zo) const {
    int hash = Hash(seed, xPrimed, yPrimed, zPrimed) & (255 << 2);

    xo = Lookup<float>::RandVecs3D[hash];
    yo = Lookup<float>::RandVecs3D[hash | 1];
    zo = Lookup<float>::RandVecs3D[hash | 2];
  }

  void GradCoordDual(int seed, int xPrimed, int yPrimed, float xd, float yd,
                     float &xo, float &yo) const {
    int hash = Hash(seed, xPrimed, yPrimed);
    int index1 = hash & (127 << 1);
    int index2 = (hash >> 7) & (255 << 1);

    float xg = Lookup<float>::Gradients2D[index1];
    float yg = Lookup<float>::Gradients2D[index1 | 1];
    float value = xd * xg + yd * yg;

    float xgo = Lookup<float>::RandVecs2D[index2];
    float ygo = Lookup<float>::RandVecs2D[index2 | 1];

    xo = value * xgo;
    yo = value * ygo;
  }

  void GradCoordDual(int seed, int xPrimed, int yPrimed, int zPrimed, float xd,
                     float yd, float zd, float &xo, float &yo,
                     float &zo) const {
    int hash = Hash(seed, xPrimed, yPrimed, zPrimed);
    int index1 = hash & (63 << 2);
    int index2 = (hash >> 6) & (255 << 2);

    float xg = Lookup<float>::Gradients3D[index1];
    float yg = Lookup<float>::Gradients3D[index1 | 1];
    float zg = Lookup<float>::Gradients3D[index1 | 2];
    float value = xd * xg + yd * yg + zd * zg;

    float xgo = Lookup<float>::RandVecs3D[index2];
    float ygo = Lookup<float>::RandVecs3D[index2 | 1];
    float zgo = Lookup<float>::RandVecs3D[index2 | 2];

    xo = value * xgo;
    yo = value * ygo;
    zo = value * zgo;
  }



  template <typename FNfloat>
  float GenNoiseSingle(int seed, FNfloat x, FNfloat y) const {
    switch (mNoiseType) {
    case NoiseType_OpenSimplex2:
      return SingleSimplex(seed, x, y);
    case NoiseType_OpenSimplex2S:
      return SingleOpenSimplex2S(seed, x, y);
    case NoiseType_Cellular:
      return SingleCellular(seed, x, y);
    case NoiseType_Perlin:
      return SinglePerlin(seed, x, y);
    case NoiseType_ValueCubic:
      return SingleValueCubic(seed, x, y);
    case NoiseType_Value:
      return SingleValue(seed, x, y);
    default:
      return 0;
    }
  }

  template <typename FNfloat>
  float GenNoiseSingle(int seed, FNfloat x, FNfloat y, FNfloat z) const {
    switch (mNoiseType) {
    case NoiseType_OpenSimplex2:
      return SingleOpenSimplex2(seed, x, y, z);
    case NoiseType_OpenSimplex2S:
      return SingleOpenSimplex2S(seed, x, y, z);
    case NoiseType_Cellular:
      return SingleCellular(seed, x, y, z);
    case NoiseType_Perlin:
      return SinglePerlin(seed, x, y, z);
    case NoiseType_ValueCubic:
      return SingleValueCubic(seed, x, y, z);
    case NoiseType_Value:
      return SingleValue(seed, x, y, z);
    default:
      return 0;
    }
  }



  template <typename FNfloat>
  void TransformNoiseCoordinate(FNfloat &x, FNfloat &y) const {
    x *= mFrequency;
    y *= mFrequency;

    switch (mNoiseType) {
    case NoiseType_OpenSimplex2:
    case NoiseType_OpenSimplex2S: {
      const FNfloat SQRT3 = (FNfloat)1.7320508075688772935274463415059;
      const FNfloat F2 = 0.5f * (SQRT3 - 1);
      FNfloat t = (x + y) * F2;
      x += t;
      y += t;
    } break;
    default:
      break;
    }
  }

  template <typename FNfloat>
  void TransformNoiseCoordinate(FNfloat &x, FNfloat &y, FNfloat &z) const {
    x *= mFrequency;
    y *= mFrequency;
    z *= mFrequency;