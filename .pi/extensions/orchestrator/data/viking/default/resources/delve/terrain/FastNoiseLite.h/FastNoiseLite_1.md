#ifndef FASTNOISELITE_H
#define FASTNOISELITE_H

#include <cmath>

class FastNoiseLite {
public:
  enum NoiseType {
    NoiseType_OpenSimplex2,
    NoiseType_OpenSimplex2S,
    NoiseType_Cellular,
    NoiseType_Perlin,
    NoiseType_ValueCubic,
    NoiseType_Value
  };

  enum RotationType3D {
    RotationType3D_None,
    RotationType3D_ImproveXYPlanes,
    RotationType3D_ImproveXZPlanes
  };

  enum FractalType {
    FractalType_None,
    FractalType_FBm,
    FractalType_Ridged,
    FractalType_PingPong,
    FractalType_DomainWarpProgressive,
    FractalType_DomainWarpIndependent
  };

  enum CellularDistanceFunction {
    CellularDistanceFunction_Euclidean,
    CellularDistanceFunction_EuclideanSq,
    CellularDistanceFunction_Manhattan,
    CellularDistanceFunction_Hybrid
  };

  enum CellularReturnType {
    CellularReturnType_CellValue,
    CellularReturnType_Distance,
    CellularReturnType_Distance2,
    CellularReturnType_Distance2Add,
    CellularReturnType_Distance2Sub,
    CellularReturnType_Distance2Mul,
    CellularReturnType_Distance2Div
  };

  enum DomainWarpType {
    DomainWarpType_OpenSimplex2,
    DomainWarpType_OpenSimplex2Reduced,
    DomainWarpType_BasicGrid
  };




  FastNoiseLite(int seed = 1337) {
    mSeed = seed;
    mFrequency = 0.01f;
    mNoiseType = NoiseType_OpenSimplex2;
    mRotationType3D = RotationType3D_None;
    mTransformType3D = TransformType3D_DefaultOpenSimplex2;

    mFractalType = FractalType_None;
    mOctaves = 3;
    mLacunarity = 2.0f;
    mGain = 0.5f;
    mWeightedStrength = 0.0f;
    mPingPongStrength = 2.0f;

    mFractalBounding = 1 / 1.75f;

    mCellularDistanceFunction = CellularDistanceFunction_EuclideanSq;
    mCellularReturnType = CellularReturnType_Distance;
    mCellularJitterModifier = 1.0f;

    mDomainWarpType = DomainWarpType_OpenSimplex2;
    mWarpTransformType3D = TransformType3D_DefaultOpenSimplex2;
    mDomainWarpAmp = 1.0f;
  }







  void SetSeed(int seed) { mSeed = seed; }







  void SetFrequency(float frequency) { mFrequency = frequency; }







  void SetNoiseType(NoiseType noiseType) {
    mNoiseType = noiseType;
    UpdateTransformType3D();
  }








  void SetRotationType3D(RotationType3D rotationType3D) {
    mRotationType3D = rotationType3D;
    UpdateTransformType3D();
    UpdateWarpTransformType3D();
  }








  void SetFractalType(FractalType fractalType) { mFractalType = fractalType; }







  void SetFractalOctaves(int octaves) {
    mOctaves = octaves;
    CalculateFractalBounding();
  }







  void SetFractalLacunarity(float lacunarity) { mLacunarity = lacunarity; }







  void SetFractalGain(float gain) {
    mGain = gain;
    CalculateFractalBounding();
  }








  void SetFractalWeightedStrength(float weightedStrength) {
    mWeightedStrength = weightedStrength;
  }







  void SetFractalPingPongStrength(float pingPongStrength) {
    mPingPongStrength = pingPongStrength;
  }







  void SetCellularDistanceFunction(
      CellularDistanceFunction cellularDistanceFunction) {
    mCellularDistanceFunction = cellularDistanceFunction;
  }







  void SetCellularReturnType(CellularReturnType cellularReturnType) {
    mCellularReturnType = cellularReturnType;
  }









  void SetCellularJitter(float cellularJitter) {
    mCellularJitterModifier = cellularJitter;
  }







  void SetDomainWarpType(DomainWarpType domainWarpType) {
    mDomainWarpType = domainWarpType;
    UpdateWarpTransformType3D();
  }








  void SetDomainWarpAmp(float domainWarpAmp) { mDomainWarpAmp = domainWarpAmp; }







  template <typename FNfloat> float GetNoise(FNfloat x, FNfloat y) const {
    Arguments_must_be_floating_point_values<FNfloat>();

    TransformNoiseCoordinate(x, y);

    switch (mFractalType) {
    default:
      return GenNoiseSingle(mSeed, x, y);
    case FractalType_FBm:
      return GenFractalFBm(x, y);
    case FractalType_Ridged:
      return GenFractalRidged(x, y);
    case FractalType_PingPong:
      return GenFractalPingPong(x, y);
    }
  }







  template <typename FNfloat>
  float GetNoise(FNfloat x, FNfloat y, FNfloat z) const {
    Arguments_must_be_floating_point_values<FNfloat>();

    TransformNoiseCoordinate(x, y, z);