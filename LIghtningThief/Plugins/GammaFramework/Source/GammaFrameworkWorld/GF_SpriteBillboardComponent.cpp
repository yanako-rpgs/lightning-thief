#include "GF_SpriteBillboardComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"

namespace
{
    // Below these the camera has not really moved and rewriting the rotation is wasted work.
    constexpr float CameraAngleTolerance = 0.01f;
    constexpr float CameraMoveTolerance = 0.5f;

    // The four grid directions laid out in 90 degree yaw order, so stepping the index by one
    // is the same as turning the camera by 90 degrees.
    // World axes per GetDirectionOffset: South = +X (yaw 0), West = +Y (yaw 90),
    // North = -X (yaw 180), East = -Y (yaw 270).
    constexpr EGF_PlayerDirection DirectionsByYaw[4] =
    {
        EGF_PlayerDirection::South,
        EGF_PlayerDirection::West,
        EGF_PlayerDirection::North,
        EGF_PlayerDirection::East
    };

    int32 YawIndexOf(EGF_PlayerDirection Direction)
    {
        for (int32 Index = 0; Index < 4; ++Index)
        {
            if (DirectionsByYaw[Index] == Direction)
            {
                return Index;
            }
        }
        return 0;
    }
}

UGF_SpriteBillboardComponent::UGF_SpriteBillboardComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;

    // Read the camera AFTER the camera manager has updated this frame, otherwise the sprite
    // is always one frame behind during a camera turn and visibly shears.
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UGF_SpriteBillboardComponent::BeginPlay()
{
    Super::BeginPlay();

    CachedTarget = ResolveTarget();
    if (!IsValid(CachedTarget))
    {
        return;
    }

    if (bUseAbsoluteRotation)
    {
        // The owner turning can no longer drag the sprite with it, so we only ever have to
        // write a rotation when the camera itself moves.
        CachedTarget->SetUsingAbsoluteRotation(true);
    }

    ApplyBillboardNow();

    if (bTrackCameraChanges)
    {
        SetComponentTickEnabled(true);
    }
}

USceneComponent* UGF_SpriteBillboardComponent::ResolveTarget() const
{
    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return nullptr;
    }

    if (USceneComponent* Picked = Cast<USceneComponent>(TargetComponent.GetComponent(Owner)))
    {
        return Picked;
    }

    return Owner->GetRootComponent();
}

APlayerCameraManager* UGF_SpriteBillboardComponent::ResolveCamera() const
{
    if (CachedCamera.IsValid())
    {
        return CachedCamera.Get();
    }

    const UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    if (const APlayerController* PC = World->GetFirstPlayerController())
    {
        CachedCamera = PC->PlayerCameraManager;
    }

    return CachedCamera.Get();
}

void UGF_SpriteBillboardComponent::RefreshBaseline()
{
    if (!IsValid(CachedTarget))
    {
        CachedTarget = ResolveTarget();
        if (!IsValid(CachedTarget))
        {
            return;
        }
    }

    const APlayerCameraManager* Camera = ResolveCamera();
    if (!Camera)
    {
        return;
    }

    BaseRotation = CachedTarget->GetComponentQuat();
    BaseCameraYaw = Camera->GetCameraRotation().Yaw;
    bBaselineCaptured = true;
}

FQuat UGF_SpriteBillboardComponent::ComputeBillboardRotation(const FVector& CameraLocation, const FRotator& CameraRotation) const
{
    if (Alignment == EGF_GESpriteAlignment::ComputeFromCamera)
    {
        // Yaw only, driven straight off the camera and ignoring whatever the sprite was
        // authored with.
        return FRotator(0.0f, CameraRotation.Yaw + YawOffset, 0.0f).Quaternion();
    }

    // Tracks the camera turning. Zero on a fixed camera, and the only term PreserveAuthored uses.
    const float DeltaYaw = FRotator::NormalizeAxis(CameraRotation.Yaw - BaseCameraYaw) + YawOffset;
    const FQuat YawTrack(FVector::UpVector, FMath::DegreesToRadians(DeltaYaw));

    if (Alignment != EGF_GESpriteAlignment::LookAtCamera || !IsValid(CachedTarget))
    {
        return YawTrack * BaseRotation;
    }

    const FVector ToCamera = (CameraLocation - CachedTarget->GetComponentLocation()).GetSafeNormal();
    if (ToCamera.IsNearlyZero())
    {
        return YawTrack * BaseRotation;
    }

    // The sprite is authored to be correct when viewed straight down the camera's forward axis,
    // so the direction it is ALREADY aiming at is the camera's forward reversed. Turning it to
    // face where the camera actually is means rotating by the gap between those two directions.
    //
    // Doing this about world up was the bug: these sprites recline to aim their normal at the
    // camera, and a vertical-axis spin swings that recline off to the side instead of keeping it
    // pointed at the camera, which foreshortens the plane flat. Building both directions into
    // full orthonormal bases and taking the difference rotates about the correct axis - and
    // because both bases are built against world up, it also comes out roll-free, so no sprite
    // ends up leaning sideways.
    const FVector ScreenCentreDir = -CameraRotation.Vector();
    const FQuat CentreBasis = FRotationMatrix::MakeFromXZ(ScreenCentreDir, FVector::UpVector).ToQuat();
    const FQuat SpriteBasis = FRotationMatrix::MakeFromXZ(ToCamera, FVector::UpVector).ToQuat();

    return (SpriteBasis * CentreBasis.Inverse()) * YawTrack * BaseRotation;
}

void UGF_SpriteBillboardComponent::ApplyBillboardNow()
{
    if (!IsValid(CachedTarget))
    {
        CachedTarget = ResolveTarget();
        if (!IsValid(CachedTarget))
        {
            return;
        }
    }

    const APlayerCameraManager* Camera = ResolveCamera();
    if (!Camera)
    {
        // No camera yet (early BeginPlay, mid level load). Tick will pick it up next frame.
        return;
    }

    if (!bBaselineCaptured)
    {
        RefreshBaseline();
    }

    const FRotator CameraRotation = Camera->GetCameraRotation();
    const FVector CameraLocation = Camera->GetCameraLocation();
    const FQuat Desired = ComputeBillboardRotation(CameraLocation, CameraRotation);

    CachedTarget->SetWorldRotation(Desired);

    LastAppliedRotation = CachedTarget->GetComponentQuat();
    LastCameraYaw = CameraRotation.Yaw;
    LastCameraLocation = CameraLocation;
    LastTargetLocation = CachedTarget->GetComponentLocation();
    bHasApplied = true;
}

void UGF_SpriteBillboardComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!IsValid(CachedTarget))
    {
        CachedTarget = ResolveTarget();
        if (!IsValid(CachedTarget))
        {
            SetComponentTickEnabled(false);
            return;
        }
    }

    const APlayerCameraManager* Camera = ResolveCamera();
    if (!Camera)
    {
        return;
    }

    const FRotator CameraRotation = Camera->GetCameraRotation();
    const FVector CameraLocation = Camera->GetCameraLocation();

    // Only LookAtCamera cares where things ARE - the other modes read the camera's yaw alone,
    // so walking around must not drag them into a recompute every frame.
    const bool bPositionMatters = (Alignment == EGF_GESpriteAlignment::LookAtCamera);

    // The common case on a fixed camera: nothing has moved, so we do a few compares and get out
    // without touching the transform.
    if (bHasApplied
        && FMath::IsNearlyEqual(CameraRotation.Yaw, LastCameraYaw, CameraAngleTolerance)
        && (!bPositionMatters
            || (CameraLocation.Equals(LastCameraLocation, CameraMoveTolerance)
                && CachedTarget->GetComponentLocation().Equals(LastTargetLocation, CameraMoveTolerance)))
        && CachedTarget->GetComponentQuat().Equals(LastAppliedRotation, KINDA_SMALL_NUMBER))
    {
        return;
    }

    ApplyBillboardNow();
}

EGF_PlayerDirection UGF_SpriteBillboardComponent::GetCameraRelativeDirection(EGF_PlayerDirection WorldDirection) const
{
    const APlayerCameraManager* Camera = ResolveCamera();
    if (!Camera || !bBaselineCaptured)
    {
        return WorldDirection;
    }

    const float DeltaYaw = FRotator::NormalizeAxis(Camera->GetCameraRotation().Yaw - BaseCameraYaw);

    // Round to the nearest quarter turn: the artwork snaps to a direction, it never smears
    // between two of them.
    const int32 CameraSteps = FMath::RoundToInt(DeltaYaw / 90.0f);
    const int32 RelativeIndex = ((YawIndexOf(WorldDirection) - CameraSteps) % 4 + 4) % 4;

    return DirectionsByYaw[RelativeIndex];
}
