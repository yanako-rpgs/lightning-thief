#include "GF_TransformShakeComponent.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

namespace
{
    // One full lean right-then-left per shake, and how hard that lean settles.
    // Tuned to read well at the fixed overworld camera - not worth exposing.
    constexpr float ShakeOscillations = 1.0f;
    constexpr float ShakeFalloff = 1.5f;
}

UGF_TransformShakeComponent::UGF_TransformShakeComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}

USceneComponent* UGF_TransformShakeComponent::ResolveTarget() const
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

void UGF_TransformShakeComponent::PlayShake()
{
    PlayShakeCustom(NumShakes, ShakeAngle, IntensityGrowth);
}

void UGF_TransformShakeComponent::PlayShakeCustom(int32 InNumShakes, float InShakeAngle, float InIntensityGrowth)
{
    // Restore first, so restarting mid-shake doesn't bake the current tilt in as the rest pose.
    StopShake();

    CachedTarget = ResolveTarget();
    if (!IsValid(CachedTarget))
    {
        return;
    }

    ActiveNumShakes = FMath::Max(1, InNumShakes);
    ActiveShakeAngle = InShakeAngle;
    ActiveIntensityGrowth = FMath::Max(1.0f, InIntensityGrowth);

    // Roll every pause now. Doing it per-tick would let a boundary move after the
    // tick had already crossed it, which re-fires or skips a step.
    {
        const float Duration = FMath::Max(0.05f, ShakeDuration);
        const float BasePause = FMath::Max(0.0f, PauseBetweenShakes);
        const float Jitter = FMath::Clamp(PauseJitter, 0.0f, 1.0f);

        ShakeStartTimes.Reset(ActiveNumShakes);

        float Cursor = 0.0f;
        for (int32 i = 0; i < ActiveNumShakes; ++i)
        {
            ShakeStartTimes.Add(Cursor);

            const float Pause = (Jitter > 0.0f)
                ? BasePause * FMath::FRandRange(1.0f - Jitter, 1.0f + Jitter)
                : BasePause;

            Cursor += Duration + FMath::Max(0.0f, Pause);
        }
    }

    InitialRotation = CachedTarget->GetRelativeRotation();
    ElapsedTime = 0.0f;
    CurrentShakeIndex = -1;
    bIsPlaying = true;

    SetComponentTickEnabled(true);
}

void UGF_TransformShakeComponent::StopShake()
{
    if (!bIsPlaying)
    {
        return;
    }

    bIsPlaying = false;
    SetComponentTickEnabled(false);

    if (IsValid(CachedTarget))
    {
        CachedTarget->SetRelativeRotation(InitialRotation);
    }
}

void UGF_TransformShakeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bIsPlaying)
    {
        return;
    }

    if (!IsValid(CachedTarget))
    {
        bIsPlaying = false;
        SetComponentTickEnabled(false);
        return;
    }

    ElapsedTime += DeltaTime;

    const float Duration = FMath::Max(0.05f, ShakeDuration);
    const int32 Count = ActiveNumShakes;

    // Walk the rolled start times rather than dividing by an average, so pauses of
    // different lengths still land each shake on its own boundary. Count is capped
    // at a handful, so a linear scan is cheaper than anything cleverer.
    int32 ShakeIndex = Count;
    for (int32 i = Count - 1; i >= 0; --i)
    {
        if (ShakeStartTimes.IsValidIndex(i) && ElapsedTime >= ShakeStartTimes[i])
        {
            ShakeIndex = i;
            break;
        }
    }

    const float LocalTime = ShakeStartTimes.IsValidIndex(ShakeIndex)
        ? ElapsedTime - ShakeStartTimes[ShakeIndex]
        : 0.0f;

    // Done: past the last shake, or sitting in the trailing pause after it.
    if (ShakeIndex >= Count || (ShakeIndex == Count - 1 && LocalTime >= Duration))
    {
        StopShake();
        OnShakeSequenceFinished.Broadcast();
        return;
    }

    if (ShakeIndex != CurrentShakeIndex)
    {
        CurrentShakeIndex = ShakeIndex;
        OnShakeStepStarted.Broadcast(ShakeIndex);
    }

    float Angle = 0.0f;

    // Zero angle during the gap between shakes leaves it resting upright.
    if (LocalTime < Duration)
    {
        const float Alpha = LocalTime / Duration;

        // Each shake is IntensityGrowth times stronger than the last.
        const float Power = ActiveShakeAngle * FMath::Pow(ActiveIntensityGrowth, static_cast<float>(ShakeIndex));

        // Full strength at the start of the shake, decaying to nothing by its end.
        const float Envelope = FMath::Pow(1.0f - Alpha, ShakeFalloff);

        Angle = Power * Envelope * FMath::Sin(Alpha * ShakeOscillations * 2.0f * PI);
    }

    const FVector Axis = RotationAxis.GetSafeNormal(KINDA_SMALL_NUMBER, FVector::ForwardVector);

    // Post-multiply so the axis is read in the target's own local space.
    const FQuat Offset(Axis, FMath::DegreesToRadians(Angle));
    CachedTarget->SetRelativeRotation(InitialRotation.Quaternion() * Offset);
}
