switch (mTransformType3D) {
    case TransformType3D_ImproveXYPlanes: {
      FNfloat xy = x + y;
      FNfloat s2 = xy * -(FNfloat)0.211324865405187;
      z *= (FNfloat)0.577350269189626;
      x += s2 - z;
      y = y + s2 - z;
      z += xy * (FNfloat)0.577350269189626;
    } break;
    case TransformType3D_ImproveXZPlanes: {
      FNfloat xz = x + z;
      FNfloat s2 = xz * -(FNfloat)0.211324865405187;
      y *= (FNfloat)0.577350269189626;
      x += s2 - y;
      z += s2 - y;
      y += xz * (FNfloat)0.577350269189626;
    } break;
    case TransformType3D_DefaultOpenSimplex2: {
      const FNfloat R3 = (FNfloat)(2.0 / 3.0);
      FNfloat r = (x + y + z) * R3;
      x = r - x;
      y = r - y;
      z = r - z;
    } break;
    default:
      break;
    }
  }

  void UpdateTransformType3D() {
    switch (mRotationType3D) {
    case RotationType3D_ImproveXYPlanes:
      mTransformType3D = TransformType3D_ImproveXYPlanes;
      break;
    case RotationType3D_ImproveXZPlanes:
      mTransformType3D = TransformType3D_ImproveXZPlanes;
      break;
    default:
      switch (mNoiseType) {
      case NoiseType_OpenSimplex2:
      case NoiseType_OpenSimplex2S:
        mTransformType3D = TransformType3D_DefaultOpenSimplex2;
        break;
      default:
        mTransformType3D = TransformType3D_None;
        break;
      }
      break;
    }
  }



  template <typename FNfloat>
  void TransformDomainWarpCoordinate(FNfloat &x, FNfloat &y) const {
    switch (mDomainWarpType) {
    case DomainWarpType_OpenSimplex2:
    case DomainWarpType_OpenSimplex2Reduced: {
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
  void TransformDomainWarpCoordinate(FNfloat &x, FNfloat &y, FNfloat &z) const {
    switch (mWarpTransformType3D) {
    case TransformType3D_ImproveXYPlanes: {
      FNfloat xy = x + y;
      FNfloat s2 = xy * -(FNfloat)0.211324865405187;
      z *= (FNfloat)0.577350269189626;
      x += s2 - z;
      y = y + s2 - z;
      z += xy * (FNfloat)0.577350269189626;
    } break;
    case TransformType3D_ImproveXZPlanes: {
      FNfloat xz = x + z;
      FNfloat s2 = xz * -(FNfloat)0.211324865405187;
      y *= (FNfloat)0.577350269189626;
      x += s2 - y;
      z += s2 - y;
      y += xz * (FNfloat)0.577350269189626;
    } break;
    case TransformType3D_DefaultOpenSimplex2: {
      const FNfloat R3 = (FNfloat)(2.0 / 3.0);
      FNfloat r = (x + y + z) * R3;
      x = r - x;
      y = r - y;
      z = r - z;
    } break;
    default:
      break;
    }
  }

  void UpdateWarpTransformType3D() {
    switch (mRotationType3D) {
    case RotationType3D_ImproveXYPlanes:
      mWarpTransformType3D = TransformType3D_ImproveXYPlanes;
      break;
    case RotationType3D_ImproveXZPlanes:
      mWarpTransformType3D = TransformType3D_ImproveXZPlanes;
      break;
    default:
      switch (mDomainWarpType) {
      case DomainWarpType_OpenSimplex2:
      case DomainWarpType_OpenSimplex2Reduced:
        mWarpTransformType3D = TransformType3D_DefaultOpenSimplex2;
        break;
      default:
        mWarpTransformType3D = TransformType3D_None;
        break;
      }
      break;
    }
  }



  template <typename FNfloat> float GenFractalFBm(FNfloat x, FNfloat y) const {
    int seed = mSeed;
    float sum = 0;
    float amp = mFractalBounding;

    for (int i = 0; i < mOctaves; i++) {
      float noise = GenNoiseSingle(seed++, x, y);
      sum += noise * amp;
      amp *= Lerp(1.0f, FastMin(noise + 1, 2) * 0.5f, mWeightedStrength);

      x *= mLacunarity;
      y *= mLacunarity;
      amp *= mGain;
    }

    return sum;
  }

  template <typename FNfloat>
  float GenFractalFBm(FNfloat x, FNfloat y, FNfloat z) const {
    int seed = mSeed;
    float sum = 0;
    float amp = mFractalBounding;

    for (int i = 0; i < mOctaves; i++) {
      float noise = GenNoiseSingle(seed++, x, y, z);
      sum += noise * amp;
      amp *= Lerp(1.0f, (noise + 1) * 0.5f, mWeightedStrength);

      x *= mLacunarity;
      y *= mLacunarity;
      z *= mLacunarity;
      amp *= mGain;
    }

    return sum;
  }



  template <typename FNfloat>
  float GenFractalRidged(FNfloat x, FNfloat y) const {
    int seed = mSeed;
    float sum = 0;
    float amp = mFractalBounding;

    for (int i = 0; i < mOctaves; i++) {
      float noise = FastAbs(GenNoiseSingle(seed++, x, y));
      sum += (noise * -2 + 1) * amp;
      amp *= Lerp(1.0f, 1 - noise, mWeightedStrength);

      x *= mLacunarity;
      y *= mLacunarity;
      amp *= mGain;
    }

    return sum;
  }