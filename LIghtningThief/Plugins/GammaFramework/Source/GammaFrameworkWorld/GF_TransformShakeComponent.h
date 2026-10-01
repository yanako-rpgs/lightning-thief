#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "GF_TransformShakeComponent.generated.h"

class USceneComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnShakeStepStarted, int32, ShakeIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGF_OnShakeSequenceFinished);

/**
 * Makes a scene component rock back and forth - egg shaking, item jiggle, etc.
 *
 * Point TargetComponent at the scene component your sprite hangs off, call PlayShake(),
 * and it rocks NumShakes times with a pause between each, every shake stronger than the
 * last. Restores the original transform when it finishes.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GAMMAFRAMEWORKWORLD_API UGF_TransformShakeComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UGF_TransformShakeComponent();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    /** The scene component to shake. Falls back to the actor's root if left empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake", meta = (UseComponentPicker, AllowedClasses = "/Script/Engine.SceneComponent"))
    FComponentReference TargetComponent;

    /** How many times it shakes per PlayShake() call. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake", meta = (ClampMin = "1", UIMin = "1", UIMax = "10"))
    int32 NumShakes = 3;

    /** Tilt of the FIRST shake, in degrees. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake", meta = (ClampMin = "0.0", UIMin = "1.0", UIMax = "45.0"))
    float ShakeAngle = 8.0f;

    /** How much stronger each shake is than the one before. 8 degrees at 1.6 gives 8, 13, 20. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake", meta = (ClampMin = "1.0", UIMin = "1.0", UIMax = "3.0"))
    float IntensityGrowth = 1.6f;

    /** Seconds one shake takes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake", AdvancedDisplay, meta = (ClampMin = "0.05", UIMin = "0.1", UIMax = "2.0"))
    float ShakeDuration = 0.4f;

    /** Seconds of stillness between shakes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "3.0"))
    float PauseBetweenShakes = 0.4f;

    /**
     * How much each pause is allowed to wander from PauseBetweenShakes, as a fraction.
     *
     * 0 is a metronome. 0.4 lets a 0.4s pause land anywhere from 0.24s to 0.56s, so
     * no two gaps match and the thing inside reads as alive rather than timed.
     *
     * Rolled once per shake when the sequence starts, never mid-flight: the tick
     * finds its step by walking the rolled pauses, and re-rolling under it would
     * move the boundaries it has already crossed.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
    float PauseJitter = 0.0f;

    /**
     * Axis the target rocks AROUND, in its own local space. The camera is fixed and Y is
     * the left/right screen axis, so rolling around X rocks it side to side.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake", AdvancedDisplay)
    FVector RotationAxis = FVector(1.0f, 0.0f, 0.0f);

    /** Shakes the target using the values above. Safe to call again while running - it restarts cleanly. */
    UFUNCTION(BlueprintCallable, Category = "Shake")
    void PlayShake();

    /**
     * Same as PlayShake, but with one-off values instead of the ones in the details panel.
     * Handy when the same egg should shake differently depending on how close it is to hatching.
     */
    UFUNCTION(BlueprintCallable, Category = "Shake")
    void PlayShakeCustom(int32 InNumShakes = 3, float InShakeAngle = 8.0f, float InIntensityGrowth = 1.6f);

    /** Cuts a shake short and snaps the target back to its resting transform. */
    UFUNCTION(BlueprintCallable, Category = "Shake")
    void StopShake();

    UFUNCTION(BlueprintPure, Category = "Shake")
    bool IsShaking() const { return bIsPlaying; }

    /** Fires as each shake begins, 0-based. Hook this to play the crack flipbook / SFX. */
    UPROPERTY(BlueprintAssignable, Category = "Shake")
    FGF_OnShakeStepStarted OnShakeStepStarted;

    /** Fires when all the shakes are done and the transform is back to normal. */
    UPROPERTY(BlueprintAssignable, Category = "Shake")
    FGF_OnShakeSequenceFinished OnShakeSequenceFinished;

private:
    USceneComponent* ResolveTarget() const;

    UPROPERTY(Transient)
    TObjectPtr<USceneComponent> CachedTarget = nullptr;

    FRotator InitialRotation = FRotator::ZeroRotator;

    // Snapshot of what this run is using, so PlayShakeCustom can override the properties
    // for one call without permanently changing them.
    int32 ActiveNumShakes = 3;
    float ActiveShakeAngle = 8.0f;
    float ActiveIntensityGrowth = 1.6f;

    // Where each shake begins, in seconds from the start of the sequence. Rolled once
    // in PlayShakeCustom so jittered pauses cannot move a boundary the tick already
    // passed -- dividing elapsed time by an average would do exactly that.
    // Always ActiveNumShakes entries; [0] is always 0.
    TArray<float> ShakeStartTimes;

    bool bIsPlaying = false;
    float ElapsedTime = 0.0f;
    int32 CurrentShakeIndex = -1;
};
