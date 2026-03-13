for (int yi = yr - 1; yi <= yr + 1; yi++) {
          int hash = Hash(seed, xPrimed, yPrimed);
          int idx = hash & (255 << 1);

          float vecX =
              (float)(xi - x) + Lookup<float>::RandVecs2D[idx] * cellularJitter;
          float vecY = (float)(yi - y) +
                       Lookup<float>::RandVecs2D[idx | 1] * cellularJitter;

          float newDistance =
              (FastAbs(vecX) + FastAbs(vecY)) + (vecX * vecX + vecY * vecY);

          distance1 = FastMax(FastMin(distance1, newDistance), distance0);
          if (newDistance < distance0) {
            distance0 = newDistance;
            closestHash = hash;
          }
          yPrimed += PrimeY;
        }
        xPrimed += PrimeX;
      }
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
  float SingleCellular(int seed, FNfloat x, FNfloat y, FNfloat z) const {
    int xr = FastRound(x);
    int yr = FastRound(y);
    int zr = FastRound(z);

    float distance0 = 1e10f;
    float distance1 = 1e10f;
    int closestHash = 0;

    float cellularJitter = 0.39614353f * mCellularJitterModifier;

    int xPrimed = (xr - 1) * PrimeX;
    int yPrimedBase = (yr - 1) * PrimeY;
    int zPrimedBase = (zr - 1) * PrimeZ;

    switch (mCellularDistanceFunction) {
    case CellularDistanceFunction_Euclidean:
    case CellularDistanceFunction_EuclideanSq:
      for (int xi = xr - 1; xi <= xr + 1; xi++) {
        int yPrimed = yPrimedBase;

        for (int yi = yr - 1; yi <= yr + 1; yi++) {
          int zPrimed = zPrimedBase;

          for (int zi = zr - 1; zi <= zr + 1; zi++) {
            int hash = Hash(seed, xPrimed, yPrimed, zPrimed);
            int idx = hash & (255 << 2);

            float vecX = (float)(xi - x) +
                         Lookup<float>::RandVecs3D[idx] * cellularJitter;
            float vecY = (float)(yi - y) +
                         Lookup<float>::RandVecs3D[idx | 1] * cellularJitter;
            float vecZ = (float)(zi - z) +
                         Lookup<float>::RandVecs3D[idx | 2] * cellularJitter;

            float newDistance = vecX * vecX + vecY * vecY + vecZ * vecZ;

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
    case CellularDistanceFunction_Manhattan:
      for (int xi = xr - 1; xi <= xr + 1; xi++) {
        int yPrimed = yPrimedBase;

        for (int yi = yr - 1; yi <= yr + 1; yi++) {
          int zPrimed = zPrimedBase;

          for (int zi = zr - 1; zi <= zr + 1; zi++) {
            int hash = Hash(seed, xPrimed, yPrimed, zPrimed);
            int idx = hash & (255 << 2);

            float vecX = (float)(xi - x) +
                         Lookup<float>::RandVecs3D[idx] * cellularJitter;
            float vecY = (float)(yi - y) +
                         Lookup<float>::RandVecs3D[idx | 1] * cellularJitter;
            float vecZ = (float)(zi - z) +
                         Lookup<float>::RandVecs3D[idx | 2] * cellularJitter;

            float newDistance = FastAbs(vecX) + FastAbs(vecY) + FastAbs(vecZ);

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
    case CellularDistanceFunction_Hybrid:
      for (int xi = xr - 1; xi <= xr + 1; xi++) {
        int yPrimed = yPrimedBase;

        for (int yi = yr - 1; yi <= yr + 1; yi++) {
          int zPrimed = zPrimedBase;

          for (int zi = zr - 1; zi <= zr + 1; zi++) {
            int hash = Hash(seed, xPrimed, yPrimed, zPrimed);
            int idx = hash & (255 << 2);