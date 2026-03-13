switch (mFractalType) {
    default:
      return GenNoiseSingle(mSeed, x, y, z);
    case FractalType_FBm:
      return GenFractalFBm(x, y, z);
    case FractalType_Ridged:
      return GenFractalRidged(x, y, z);
    case FractalType_PingPong:
      return GenFractalPingPong(x, y, z);
    }
  }









  template <typename FNfloat> void DomainWarp(FNfloat &x, FNfloat &y) const {
    Arguments_must_be_floating_point_values<FNfloat>();

    switch (mFractalType) {
    default:
      DomainWarpSingle(x, y);
      break;
    case FractalType_DomainWarpProgressive:
      DomainWarpFractalProgressive(x, y);
      break;
    case FractalType_DomainWarpIndependent:
      DomainWarpFractalIndependent(x, y);
      break;
    }
  }









  template <typename FNfloat>
  void DomainWarp(FNfloat &x, FNfloat &y, FNfloat &z) const {
    Arguments_must_be_floating_point_values<FNfloat>();

    switch (mFractalType) {
    default:
      DomainWarpSingle(x, y, z);
      break;
    case FractalType_DomainWarpProgressive:
      DomainWarpFractalProgressive(x, y, z);
      break;
    case FractalType_DomainWarpIndependent:
      DomainWarpFractalIndependent(x, y, z);
      break;
    }
  }

private:
  template <typename T> struct Arguments_must_be_floating_point_values;

  enum TransformType3D {
    TransformType3D_None,
    TransformType3D_ImproveXYPlanes,
    TransformType3D_ImproveXZPlanes,
    TransformType3D_DefaultOpenSimplex2
  };

  int mSeed;
  float mFrequency;
  NoiseType mNoiseType;
  RotationType3D mRotationType3D;
  TransformType3D mTransformType3D;

  FractalType mFractalType;
  int mOctaves;
  float mLacunarity;
  float mGain;
  float mWeightedStrength;
  float mPingPongStrength;

  float mFractalBounding;

  CellularDistanceFunction mCellularDistanceFunction;
  CellularReturnType mCellularReturnType;
  float mCellularJitterModifier;

  DomainWarpType mDomainWarpType;
  TransformType3D mWarpTransformType3D;
  float mDomainWarpAmp;

  template <typename T> struct Lookup {
    static const T Gradients2D[];
    static const T Gradients3D[];
    static const T RandVecs2D[];
    static const T RandVecs3D[];
  };

  static float FastMin(float a, float b) { return a < b ? a : b; }

  static float FastMax(float a, float b) { return a > b ? a : b; }

  static float FastAbs(float f) { return f < 0 ? -f : f; }

  static float FastSqrt(float f) { return sqrtf(f); }

  template <typename FNfloat> static int FastFloor(FNfloat f) {
    return f >= 0 ? (int)f : (int)f - 1;
  }

  template <typename FNfloat> static int FastRound(FNfloat f) {
    return f >= 0 ? (int)(f + 0.5f) : (int)(f - 0.5f);
  }

  static float Lerp(float a, float b, float t) { return a + t * (b - a); }

  static float InterpHermite(float t) { return t * t * (3 - 2 * t); }

  static float InterpQuintic(float t) {
    return t * t * t * (t * (t * 6 - 15) + 10);
  }

  static float CubicLerp(float a, float b, float c, float d, float t) {
    float p = (d - c) - (a - b);
    return t * t * t * p + t * t * ((a - b) - p) + t * (c - a) + b;
  }

  static float PingPong(float t) {
    t -= (int)(t * 0.5f) * 2;
    return t < 1 ? t : 2 - t;
  }

  void CalculateFractalBounding() {
    float gain = FastAbs(mGain);
    float amp = gain;
    float ampFractal = 1.0f;
    for (int i = 1; i < mOctaves; i++) {
      ampFractal += amp;
      amp *= gain;
    }
    mFractalBounding = 1 / ampFractal;
  }


  static const int PrimeX = 501125321;
  static const int PrimeY = 1136930381;
  static const int PrimeZ = 1720413743;

  static int Hash(int seed, int xPrimed, int yPrimed) {
    int hash = seed ^ xPrimed ^ yPrimed;

    hash *= 0x27d4eb2d;
    return hash;
  }

  static int Hash(int seed, int xPrimed, int yPrimed, int zPrimed) {
    int hash = seed ^ xPrimed ^ yPrimed ^ zPrimed;

    hash *= 0x27d4eb2d;
    return hash;
  }

  static float ValCoord(int seed, int xPrimed, int yPrimed) {
    int hash = Hash(seed, xPrimed, yPrimed);

    hash *= hash;
    hash ^= hash << 19;
    return hash * (1 / 2147483648.0f);
  }

  static float ValCoord(int seed, int xPrimed, int yPrimed, int zPrimed) {
    int hash = Hash(seed, xPrimed, yPrimed, zPrimed);

    hash *= hash;
    hash ^= hash << 19;
    return hash * (1 / 2147483648.0f);
  }

  float GradCoord(int seed, int xPrimed, int yPrimed, float xd,
                  float yd) const {
    int hash = Hash(seed, xPrimed, yPrimed);
    hash ^= hash >> 15;
    hash &= 127 << 1;