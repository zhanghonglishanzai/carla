// CARLA, Copyright (C) 2017 Computer Vision Center (CVC)

#include "Carla.h"
#include "RoadMap.h"

#include "DrawDebugHelpers.h"
#include "HighResScreenshot.h"

static uint32 ClampFloatToUInt(const float Value, int32 Min, int32 Max)
{
  return FMath::Clamp(FMath::FloorToInt(Value), Min, Max);
}

// Creates a valid empty map (every point is off-road).
URoadMap::URoadMap(const FObjectInitializer& ObjectInitializer) :
  Super(ObjectInitializer),
  PixelsPerCentimeter(1.0f),
  Width(1u),
  Height(1u)
{
  AppendEmptyPixel();
}

FVector URoadMap::GetWorldLocation(uint32 PixelX, uint32 PixelY) const
{
  const FVector RelativePosition(
      static_cast<float>(PixelX) / PixelsPerCentimeter,
      static_cast<float>(PixelY) / PixelsPerCentimeter,
      0.0f);
  return WorldToMap.InverseTransformPosition(RelativePosition + MapOffset);
}

const FRoadMapPixelData &URoadMap::GetDataAt(const FVector &WorldLocation) const
{
  check(IsValid());
  const FVector Location = WorldToMap.TransformPosition(WorldLocation) - MapOffset;
  uint32 X = ClampFloatToUInt(PixelsPerCentimeter * Location.X, 0, Width - 1);
  uint32 Y = ClampFloatToUInt(PixelsPerCentimeter * Location.Y, 0, Height - 1);
  return GetDataAt(X, Y);
}

FRoadMapIntersectionResult URoadMap::Intersect(
    const FTransform &BoxTransform,
    const FVector &BoxExtent,
    float ChecksPerCentimeter) const
{
  const auto &DirectionOfMovement = BoxTransform.GetRotation().GetForwardVector();
  uint32 CheckCount = 0u;
  FRoadMapIntersectionResult Result = {0.0f, 0.0f};
  const float Step = 1.0f / ChecksPerCentimeter;
  for (float X = -BoxExtent.X; X < BoxExtent.X; X += Step) {
    for (float Y = -BoxExtent.Y; Y < BoxExtent.Y; Y += Step) {
      ++CheckCount;
      auto Location = BoxTransform.TransformPosition(FVector(X, Y, 0.0f));
      auto &Data = GetDataAt(Location);
      if (Data.bIsOffRoad) {
        Result.OffRoad += 1.0f;
      } else if (Data.bHasDirection &&
                 0.0f < FVector::DotProduct(Data.Direction, DirectionOfMovement)) {
        Result.OppositeLane += 1.0f;
      }
    }
  }
  if (CheckCount > 0u) {
    Result.OffRoad /= static_cast<float>(CheckCount);
    Result.OppositeLane /= static_cast<float>(CheckCount);
  } else {
    UE_LOG(LogCarla, Warning, TEXT("URoadMap::Intersect did zero checks"));
  }
  return Result;
}

static FColor Encode(const FRoadMapPixelData &Data)
{
  if (Data.bIsOffRoad) {
    return FColor(0u, 0u, 0u, 255u);
  } else if (!Data.bHasDirection) {
    return FColor(255u, 255u, 255u, 255u);
  } else {
    // Assumes normalized direction.
    auto ToColor = [](float X){
      return FMath::FloorToInt(255.0 * (X + 1.0f) / 2.0f);
    };
    return FColor(ToColor(Data.Direction.X), ToColor(Data.Direction.Y), ToColor(Data.Direction.Z));
  }
}

bool URoadMap::SaveAsPNG(const FString &Path) const
{
  if (!IsValid()) {
    UE_LOG(LogCarla, Error, TEXT("Cannot save invalid road map to disk"));
    return false;
  }

  TArray<FColor> BitMap;
  for (auto &Data : RoadMap) {
    BitMap.Emplace(Encode(Data));
  }

  FIntPoint DestSize(Width, Height);
  FString ResultPath;
  FHighResScreenshotConfig& HighResScreenshotConfig = GetHighResScreenshotConfig();
  HighResScreenshotConfig.SaveImage(Path, BitMap, DestSize, &ResultPath);
  UE_LOG(LogCarla, Log, TEXT("Saved road map to \"%s\""), *ResultPath);
  return true;
}

void URoadMap::DrawDebugPixelsToLevel(UWorld *World, const bool bJustFlushDoNotDraw) const
{
  FlushPersistentDebugLines(World);
  if (!bJustFlushDoNotDraw) {
    for (auto X = 0u; X < Width; ++X) {
      for (auto Y = 0u; Y < Height; ++Y) {
        auto Location = GetWorldLocation(X, Y);
        auto Color = Encode(GetDataAt(X, Y));
        DrawDebugPoint(World, Location, 20.0f, Color, true);
      }
    }
  }
}

void URoadMap::AppendPixel(
    ECityMapMeshTag Tag,
    const FTransform &Transform,
    const bool bInvertDirection)
{
  AppendEmptyPixel();
  auto &Data = RoadMap.Last();
  Data.bIsOffRoad = false;

  auto Rotator = Transform.GetRotation().Rotator();
  switch (Tag) {
    case ECityMapMeshTag::RoadTwoLanes_LaneRight:
    case ECityMapMeshTag::Road90DegTurn_Lane0:
      Data.bHasDirection = true;
      break;
    case ECityMapMeshTag::RoadTwoLanes_LaneLeft:
    case ECityMapMeshTag::Road90DegTurn_Lane1:
      Rotator.Yaw += 180.0f;
      Data.bHasDirection = true;
      break;
    case ECityMapMeshTag::Road90DegTurn_Lane2:
      Rotator.Yaw += 90.0f;
      Data.bHasDirection = true;
      break;
    case ECityMapMeshTag::Road90DegTurn_Lane3:
      Rotator.Yaw += 270.0f;
      Data.bHasDirection = true;
      break;
  }
  if (Data.bHasDirection) {
    FQuat Rotation(Rotator);
    Data.Direction = Rotation.GetForwardVector();
    if (bInvertDirection) {
      Data.Direction *= -1.0f;
    }
  }
}

void URoadMap::Set(
    const uint32 inWidth,
    const uint32 inHeight,
    const float inPinxelsPerCentimeter,
    const FTransform &inWorldToMap,
    const FVector &inMapOffset)
{
  Width = inWidth;
  Height = inHeight;
  PixelsPerCentimeter = inPinxelsPerCentimeter;
  WorldToMap = inWorldToMap;
  MapOffset = inMapOffset;
}