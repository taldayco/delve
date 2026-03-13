bool skip9 = false;
    float a6 = yAFlipMask0 + a0;
    if (a6 > 0) {
      float x6 = x0;
      float y6 = y0 - (yNMask | 1);
      float z6 = z0;
      value += (a6 * a6) * (a6 * a6) *
               GradCoord(seed, i + (xNMask & PrimeX), j + (~yNMask & PrimeY),
                         k + (zNMask & PrimeZ), x6, y6, z6);
    } else {
      float a7 = xAFlipMask0 + zAFlipMask0 + a0;
      if (a7 > 0) {
        float x7 = x0 - (xNMask | 1);
        float y7 = y0;
        float z7 = z0 - (zNMask | 1);
        value += (a7 * a7) * (a7 * a7) *
                 GradCoord(seed, i + (~xNMask & PrimeX), j + (yNMask & PrimeY),
                           k + (~zNMask & PrimeZ), x7, y7, z7);
      }

      float a8 = yAFlipMask1 + a1;
      if (a8 > 0) {
        float x8 = x1;
        float y8 = (yNMask | 1) + y1;
        float z8 = z1;
        value += (a8 * a8) * (a8 * a8) *
                 GradCoord(seed2, i + PrimeX, j + (yNMask & (PrimeY << 1)),
                           k + PrimeZ, x8, y8, z8);
        skip9 = true;
      }
    }

    bool skipD = false;
    float aA = zAFlipMask0 + a0;
    if (aA > 0) {
      float xA = x0;
      float yA = y0;
      float zA = z0 - (zNMask | 1);
      value += (aA * aA) * (aA * aA) *
               GradCoord(seed, i + (xNMask & PrimeX), j + (yNMask & PrimeY),
                         k + (~zNMask & PrimeZ), xA, yA, zA);
    } else {
      float aB = xAFlipMask0 + yAFlipMask0 + a0;
      if (aB > 0) {
        float xB = x0 - (xNMask | 1);
        float yB = y0 - (yNMask | 1);
        float zB = z0;
        value += (aB * aB) * (aB * aB) *
                 GradCoord(seed, i + (~xNMask & PrimeX), j + (~yNMask & PrimeY),
                           k + (zNMask & PrimeZ), xB, yB, zB);
      }

      float aC = zAFlipMask1 + a1;
      if (aC > 0) {
        float xC = x1;
        float yC = y1;
        float zC = (zNMask | 1) + z1;
        value += (aC * aC) * (aC * aC) *
                 GradCoord(seed2, i + PrimeX, j + PrimeY,
                           k + (zNMask & (PrimeZ << 1)), xC, yC, zC);
        skipD = true;
      }
    }

    if (!skip5) {
      float a5 = yAFlipMask1 + zAFlipMask1 + a1;
      if (a5 > 0) {
        float x5 = x1;
        float y5 = (yNMask | 1) + y1;
        float z5 = (zNMask | 1) + z1;
        value += (a5 * a5) * (a5 * a5) *
                 GradCoord(seed2, i + PrimeX, j + (yNMask & (PrimeY << 1)),
                           k + (zNMask & (PrimeZ << 1)), x5, y5, z5);
      }
    }

    if (!skip9) {
      float a9 = xAFlipMask1 + zAFlipMask1 + a1;
      if (a9 > 0) {
        float x9 = (xNMask | 1) + x1;
        float y9 = y1;
        float z9 = (zNMask | 1) + z1;
        value += (a9 * a9) * (a9 * a9) *
                 GradCoord(seed2, i + (xNMask & (PrimeX * 2)), j + PrimeY,
                           k + (zNMask & (PrimeZ << 1)), x9, y9, z9);
      }
    }

    if (!skipD) {
      float aD = xAFlipMask1 + yAFlipMask1 + a1;
      if (aD > 0) {
        float xD = (xNMask | 1) + x1;
        float yD = (yNMask | 1) + y1;
        float zD = z1;
        value +=
            (aD * aD) * (aD * aD) *
            GradCoord(seed2, i + (xNMask & (PrimeX << 1)),
                      j + (yNMask & (PrimeY << 1)), k + PrimeZ, xD, yD, zD);
      }
    }

    return value * 9.046026385208288f;
  }



  template <typename FNfloat>
  float SingleCellular(int seed, FNfloat x, FNfloat y) const {
    int xr = FastRound(x);
    int yr = FastRound(y);

    float distance0 = 1e10f;
    float distance1 = 1e10f;
    int closestHash = 0;

    float cellularJitter = 0.43701595f * mCellularJitterModifier;

    int xPrimed = (xr - 1) * PrimeX;
    int yPrimedBase = (yr - 1) * PrimeY;

    switch (mCellularDistanceFunction) {
    default:
    case CellularDistanceFunction_Euclidean:
    case CellularDistanceFunction_EuclideanSq:
      for (int xi = xr - 1; xi <= xr + 1; xi++) {
        int yPrimed = yPrimedBase;

        for (int yi = yr - 1; yi <= yr + 1; yi++) {
          int hash = Hash(seed, xPrimed, yPrimed);
          int idx = hash & (255 << 1);

          float vecX =
              (float)(xi - x) + Lookup<float>::RandVecs2D[idx] * cellularJitter;
          float vecY = (float)(yi - y) +
                       Lookup<float>::RandVecs2D[idx | 1] * cellularJitter;

          float newDistance = vecX * vecX + vecY * vecY;

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
    case CellularDistanceFunction_Manhattan:
      for (int xi = xr - 1; xi <= xr + 1; xi++) {
        int yPrimed = yPrimedBase;

        for (int yi = yr - 1; yi <= yr + 1; yi++) {
          int hash = Hash(seed, xPrimed, yPrimed);
          int idx = hash & (255 << 1);

          float vecX =
              (float)(xi - x) + Lookup<float>::RandVecs2D[idx] * cellularJitter;
          float vecY = (float)(yi - y) +
                       Lookup<float>::RandVecs2D[idx | 1] * cellularJitter;

          float newDistance = FastAbs(vecX) + FastAbs(vecY);

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
    case CellularDistanceFunction_Hybrid:
      for (int xi = xr - 1; xi <= xr + 1; xi++) {
        int yPrimed = yPrimedBase;