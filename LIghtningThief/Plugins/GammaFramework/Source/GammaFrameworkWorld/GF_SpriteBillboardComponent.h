#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "GF_GridWorldSubsystem.h"
#include "GF_SpriteBillboardComponent.generated.h"

class APlayerCameraManager;
class USceneComponent;

UENUM(BlueprintType)
enum class EGF_GESpriteAlignment : uint8
{
    /**
     * Keeps whatever rotation the sprite has in the editor and only applies the camera's
     * yaw CHANGE on top of it. Nothing looks different until the camera actually turns,
     * so it can never come out backwards. Use this on existing actors.
     */
    PreserveAuthored,

    /**
     * Ignores the authored rotation and drives yaw straight off the camera. Use this on
     * new sprites that have not been rotated by hand, then dial YawOffset in 90 degree
     * steps until they face front.
     */
    ComputeFromCamera,

    /**
     * Yaws each sprite at the camera's LOCATION instead of aligning it to the view plane.
     *
     * This is the one that fixes the "cardboard" look on a wide-FOV perspective camera: a
     * sprite off at the screen edge is otherwise viewed obliquely and renders squashed, and
     * pointing it at the camera puts it dead-on wherever it sits on screen.
     *
     * The rotation is invisible as long as the camera only ever translates - a flat plane
     * whose normal always points at you looks identical however it is yawed. It only starts
     * to show if the camera can ORBIT, or via secondary cues (a dynamic shadow turning, a
     * sprite rotating its footprint into a wall it is standing against).
     *
     * Still yaw only, and still layered on the authored rotation, so any tilt baked into the
     * sprite survives untouched.
     */
    LookAtCamera
};

/**
 * Turns a sprite to face the camera without the "flapping" you get from a naive look-at.
 *
 * The trick is that this aligns the sprite to the camera's view PLANE (its yaw) rather than
 * pointing it at the camera's LOCATION. Every billboarded sprite in the level therefore ends
 * up parallel to every other one, so nothing counter-rotates as the player walks past - the
 * art just reads as flat. Pitch and roll are left alone so feet stay planted on the tile.
 *
 * With the fixed overworld camera this costs nothing: it sets the rotation once and then only
 * writes again if the camera actually turns.
 *
 * Point TargetComponent at the sprite (or better, at a pivot scene component above it) - NOT
 * at a collision root, or you will rotate the capsule too.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GAMMAFRAMEWORKWORLD_API UGF_SpriteBillboardComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UGF_SpriteBillboardComponent();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    /** The scene component to turn. Falls back to the actor's root if left empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Billboard", meta = (UseComponentPicker, AllowedClasses = "/Script/Engine.SceneComponent"))
    FComponentReference TargetComponent;

    /** How the facing rotation is worked out. See the enum for which one you want. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Billboard")
    EGF_GESpriteAlignment Alignment = EGF_GESpriteAlignment::PreserveAuthored;

    /** Extra yaw laid on top of the result, in degrees. Only needed if the sprite reads edge-on or mirrored. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Billboard", meta = (ClampMin = "-180.0", ClampMax = "180.0"))
    float YawOffset = 0.0f;

    /**
     * Keeps following the camera after the first alignment. Leave on - the check is a float
     * compare and it only writes a rotation when the camera has genuinely turned.
     * Turn off only if you are certain the camera never moves and want the tick gone entirely.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Billboard", AdvancedDisplay)
    bool bTrackCameraChanges = true;

    /**
     * Detaches the sprite's rotation from its parent's, so the owner turning cannot drag the
     * sprite off-axis. Safe to leave on. Switch off if something else drives this component's
     * relative rotation (a shake, a tilt) and you would rather billboard a pivot above it.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Billboard", AdvancedDisplay)
    bool bUseAbsoluteRotation = true;

    /** Re-aligns to the camera right now. Call after a scripted camera cut if the sprite lags a frame. */
    UFUNCTION(BlueprintCallable, Category = "Billboard")
    void ApplyBillboardNow();

    /**
     * Treats the sprite's current look as the new neutral. Call this if a cutscene leaves the
     * camera at a different yaw that you want to become "normal" from now on.
     * Only does anything in PreserveAuthored mode.
     */
    UFUNCTION(BlueprintCallable, Category = "Billboard")
    void RefreshBaseline();

    /**
     * Converts a world facing into the direction it APPEARS to face from where the camera is now.
     *
     * Only useful if you ever let the camera orbit. Feed this into the flipbook selector instead
     * of the raw facing and the artwork swaps while the sprite card itself never visibly turns -
     * which is the whole trick behind 8-direction 2.5D. With the fixed camera it returns the
     * direction unchanged, so it is safe to wire in ahead of time.
     */
    UFUNCTION(BlueprintPure, Category = "Billboard")
    EGF_PlayerDirection GetCameraRelativeDirection(EGF_PlayerDirection WorldDirection) const;

private:
    USceneComponent* ResolveTarget() const;
    APlayerCameraManager* ResolveCamera() const;

    /** Builds the world rotation the sprite should be sitting at for the camera's current pose. */
    FQuat ComputeBillboardRotation(const FVector& CameraLocation, const FRotator& CameraRotation) const;

    UPROPERTY(Transient)
    TObjectPtr<USceneComponent> CachedTarget = nullptr;

    // Weak rather than a UPROPERTY so the const accessors can fill it in without a const_cast.
    mutable TWeakObjectPtr<APlayerCameraManager> CachedCamera;

    /** Rotation the sprite was authored with, and the camera yaw that went with it. */
    FQuat BaseRotation = FQuat::Identity;
    float BaseCameraYaw = 0.0f;

    /** What we last wrote, so we can tell the sprite has been knocked off-axis by something else. */
    FQuat LastAppliedRotation = FQuat::Identity;
    float LastCameraYaw = 0.0f;

    // LookAtCamera depends on where the camera and the sprite ARE, not just which way the
    // camera points, so both positions have to be part of the "has anything moved" check.
    FVector LastCameraLocation = FVector::ZeroVector;
    FVector LastTargetLocation = FVector::ZeroVector;

    bool bBaselineCaptured = false;
    bool bHasApplied = false;
};
