float vecX = (float)(xi - x) +
                         Lookup<float>::RandVecs3D[idx] * cellularJitter;
            float vecY = (float)(yi - y) +
                         Lookup<float>::RandVecs3D[idx | 1] * cellularJitter;
            float vecZ = (float)(zi - z) +
                         Lookup<float>::RandVecs3D[idx | 2] * cellularJitter;

            float newDistance =
                (FastAbs(vecX) + FastAbs(vecY) + FastAbs(vecZ)) +
                (vecX * vecX + vecY * vecY + vecZ * vecZ);

            distance1 = FastMax(FastMin(distance1, newDistance), distance0);
            if (newDistance < distance0) {
              distance0 = newDistance;
              closestHash = hash;
            }
            zPrimed += PrimeZ;
          }
          yPrimed += PrimeY;
        }
        xPrimed += PrimeX;
      }
      break;
    default:
      break;
    }

    if (mCellularDistanceFunction == CellularDistanceFunction_Euclidean &&
        mCellularReturnType >= CellularReturnType_Distance) {
      distance0 = FastSqrt(distance0);

      if (mCellularReturnType >= CellularReturnType_Distance2) {
        distance1 = FastSqrt(distance1);
      }
    }

    switch (mCellularReturnType) {
    case CellularReturnType_CellValue:
      return closestHash * (1 / 2147483648.0f);
    case CellularReturnType_Distance:
      return distance0 - 1;
    case CellularReturnType_Distance2:
      return distance1 - 1;
    case CellularReturnType_Distance2Add:
      return (distance1 + distance0) * 0.5f - 1;
    case CellularReturnType_Distance2Sub:
      return distance1 - distance0 - 1;
    case CellularReturnType_Distance2Mul:
      return distance1 * distance0 * 0.5f - 1;
    case CellularReturnType_Distance2Div:
      return distance0 / distance1 - 1;
    default:
      return 0;
    }
  }



  template <typename FNfloat>
  float SinglePerlin(int seed, FNfloat x, FNfloat y) const {
    int x0 = FastFloor(x);
    int y0 = FastFloor(y);

    float xd0 = (float)(x - x0);
    float yd0 = (float)(y - y0);
    float xd1 = xd0 - 1;
    float yd1 = yd0 - 1;

    float xs = InterpQuintic(xd0);
    float ys = InterpQuintic(yd0);

    x0 *= PrimeX;
    y0 *= PrimeY;
    int x1 = x0 + PrimeX;
    int y1 = y0 + PrimeY;

    float xf0 = Lerp(GradCoord(seed, x0, y0, xd0, yd0),
                     GradCoord(seed, x1, y0, xd1, yd0), xs);
    float xf1 = Lerp(GradCoord(seed, x0, y1, xd0, yd1),
                     GradCoord(seed, x1, y1, xd1, yd1), xs);

    return Lerp(xf0, xf1, ys) * 1.4247691104677813f;
  }

  template <typename FNfloat>
  float SinglePerlin(int seed, FNfloat x, FNfloat y, FNfloat z) const {
    int x0 = FastFloor(x);
    int y0 = FastFloor(y);
    int z0 = FastFloor(z);

    float xd0 = (float)(x - x0);
    float yd0 = (float)(y - y0);
    float zd0 = (float)(z - z0);
    float xd1 = xd0 - 1;
    float yd1 = yd0 - 1;
    float zd1 = zd0 - 1;

    float xs = InterpQuintic(xd0);
    float ys = InterpQuintic(yd0);
    float zs = InterpQuintic(zd0);

    x0 *= PrimeX;
    y0 *= PrimeY;
    z0 *= PrimeZ;
    int x1 = x0 + PrimeX;
    int y1 = y0 + PrimeY;
    int z1 = z0 + PrimeZ;

    float xf00 = Lerp(GradCoord(seed, x0, y0, z0, xd0, yd0, zd0),
                      GradCoord(seed, x1, y0, z0, xd1, yd0, zd0), xs);
    float xf10 = Lerp(GradCoord(seed, x0, y1, z0, xd0, yd1, zd0),
                      GradCoord(seed, x1, y1, z0, xd1, yd1, zd0), xs);
    float xf01 = Lerp(GradCoord(seed, x0, y0, z1, xd0, yd0, zd1),
                      GradCoord(seed, x1, y0, z1, xd1, yd0, zd1), xs);
    float xf11 = Lerp(GradCoord(seed, x0, y1, z1, xd0, yd1, zd1),
                      GradCoord(seed, x1, y1, z1, xd1, yd1, zd1), xs);

    float yf0 = Lerp(xf00, xf10, ys);
    float yf1 = Lerp(xf01, xf11, ys);

    return Lerp(yf0, yf1, zs) * 0.964921414852142333984375f;
  }



  template <typename FNfloat>
  float SingleValueCubic(int seed, FNfloat x, FNfloat y) const {
    int x1 = FastFloor(x);
    int y1 = FastFloor(y);

    float xs = (float)(x - x1);
    float ys = (float)(y - y1);

    x1 *= PrimeX;
    y1 *= PrimeY;
    int x0 = x1 - PrimeX;
    int y0 = y1 - PrimeY;
    int x2 = x1 + PrimeX;
    int y2 = y1 + PrimeY;
    int x3 = x1 + (int)((long)PrimeX << 1);
    int y3 = y1 + (int)((long)PrimeY << 1);

    return CubicLerp(
               CubicLerp(ValCoord(seed, x0, y0), ValCoord(seed, x1, y0),
                         ValCoord(seed, x2, y0), ValCoord(seed, x3, y0), xs),
               CubicLerp(ValCoord(seed, x0, y1), ValCoord(seed, x1, y1),
                         ValCoord(seed, x2, y1), ValCoord(seed, x3, y1), xs),
               CubicLerp(ValCoord(seed, x0, y2), ValCoord(seed, x1, y2),
                         ValCoord(seed, x2, y2), ValCoord(seed, x3, y2), xs),
               CubicLerp(ValCoord(seed, x0, y3), ValCoord(seed, x1, y3),
                         ValCoord(seed, x2, y3), ValCoord(seed, x3, y3), xs),
               ys) *
           (1 / (1.5f * 1.5f));
  }