#include "GF_MeleeStagingLibrary.h"

#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "PaperSprite.h"

namespace
{
	/**
	 * The bounding rectangle of a sprite's quad in the flipbook's LOCAL space.
	 *
	 * Paper2D lays its quads out in the XZ plane, and BakedRenderData holds the render triangles
	 * with XY as that local XZ position relative to the pivot. So the returned Min/Max are
	 * (local X, local Z). Deliberately NOT GetSourceSize() -- that is editor-only data and would
	 * compile here then come back zero in a cooked build.
	 */
	bool MeasureSpriteQuad(UPaperSprite* Sprite, FVector2D& OutMin, FVector2D& OutMax)
	{
		if (!IsValid(Sprite) || Sprite->BakedRenderData.Num() < 3)
		{
			return false;
		}

		FVector2D Min(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
		FVector2D Max(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest());

		for (const FVector4& Vertex : Sprite->BakedRenderData)
		{
			Min.X = FMath::Min(Min.X, Vertex.X);
			Min.Y = FMath::Min(Min.Y, Vertex.Y);
			Max.X = FMath::Max(Max.X, Vertex.X);
			Max.Y = FMath::Max(Max.Y, Vertex.Y);
		}

		OutMin = Min;
		OutMax = Max;
		return true;
	}

	/** Union of every frame in a flipbook, so the answer does not change as the animation plays. */
	bool MeasureFlipbookQuad(UPaperFlipbook* Flipbook, bool bAllFrames, float PlaybackTime, FVector2D& OutMin, FVector2D& OutMax)
	{
		if (!IsValid(Flipbook))
		{
			return false;
		}

		if (!bAllFrames)
		{
			return MeasureSpriteQuad(Flipbook->GetSpriteAtTime(PlaybackTime, /*bClampToEnds=*/true), OutMin, OutMax);
		}

		bool bAny = false;
		FVector2D Min(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
		FVector2D Max(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest());

		const int32 NumFrames = Flipbook->GetNumFrames();
		for (int32 Frame = 0; Frame < NumFrames; ++Frame)
		{
			// A keyframe is allowed to hold no sprite -- that is how a blink or a gap is authored.
			FVector2D FrameMin, FrameMax;
			if (MeasureSpriteQuad(Flipbook->GetSpriteAtFrame(Frame), FrameMin, FrameMax))
			{
				Min.X = FMath::Min(Min.X, FrameMin.X);
				Min.Y = FMath::Min(Min.Y, FrameMin.Y);
				Max.X = FMath::Max(Max.X, FrameMax.X);
				Max.Y = FMath::Max(Max.Y, FrameMax.Y);
				bAny = true;
			}
		}

		if (bAny)
		{
			OutMin = Min;
			OutMax = Max;
		}
		return bAny;
	}

	/** Horizontal unit vector pointing from a world location back at the player's camera. */
	FVector HorizontalToCamera(const UObject* WorldContext, const FVector& From)
	{
		APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(WorldContext, 0);
		if (!IsValid(Camera))
		{
			return FVector::ZeroVector;
		}

		FVector ToCamera = Camera->GetCameraLocation() - From;
		ToCamera.Z = 0.0;
		return ToCamera.GetSafeNormal();
	}
}

FGF_SpriteFootprint UGF_MeleeStagingLibrary::GetSpriteFootprint(
	UPaperFlipbookComponent* FlipbookComponent,
	bool bUseWidestFrame,
	UPaperFlipbook* MeasureFlipbook)
{
	FGF_SpriteFootprint Out;

	if (!IsValid(FlipbookComponent))
	{
		return Out;
	}

	UPaperFlipbook* Flipbook = IsValid(MeasureFlipbook) ? MeasureFlipbook : FlipbookComponent->GetFlipbook();

	FVector2D Min, Max;
	if (!MeasureFlipbookQuad(Flipbook, bUseWidestFrame, FlipbookComponent->GetPlaybackPosition(), Min, Max))
	{
		UE_LOG(LogTemp, Warning, TEXT("GF_MeleeStaging: '%s' has no measurable sprite frames."), *FlipbookComponent->GetName());
		return Out;
	}

	// Measure in world space rather than scaling the local numbers by hand: the battle sprites are
	// authored pre-tilted to face the fixed camera, and the component transform carries that tilt,
	// the pivot offset and whatever scale the creature was spawned at, all at once.
	const FTransform& ToWorld = FlipbookComponent->GetComponentTransform();
	const FVector2D CenterLocal = (Min + Max) * 0.5;

	const FVector LeftWorld = ToWorld.TransformPosition(FVector(Min.X, 0.0, CenterLocal.Y));
	const FVector RightWorld = ToWorld.TransformPosition(FVector(Max.X, 0.0, CenterLocal.Y));
	const FVector TopWorld = ToWorld.TransformPosition(FVector(CenterLocal.X, 0.0, Max.Y));
	const FVector BottomWorld = ToWorld.TransformPosition(FVector(CenterLocal.X, 0.0, Min.Y));

	FVector RightAxis = RightWorld - LeftWorld;
	Out.HalfWidth = static_cast<float>(RightAxis.Size() * 0.5);

	// Flatten the right axis: the authored tilt is a recline about the sprite's own right axis, so
	// this is already horizontal in practice. Flattening anyway costs nothing and stops a
	// hand-rotated sprite from tipping the landing spot up or down out of the board plane.
	RightAxis.Z = 0.0;
	Out.RightAxis = RightAxis.GetSafeNormal(SMALL_NUMBER, FVector::ForwardVector);

	Out.Height = static_cast<float>((TopWorld - BottomWorld).Size());
	Out.Center = ToWorld.TransformPosition(FVector(CenterLocal.X, 0.0, CenterLocal.Y));
	Out.Feet = BottomWorld;
	Out.bValid = true;
	return Out;
}

FVector UGF_MeleeStagingLibrary::GetMeleeApproachLocation(
	UPaperFlipbookComponent* AttackerFlipbook,
	UPaperFlipbookComponent* TargetFlipbook,
	float Gap,
	bool bMatchTargetGround,
	float DepthBias,
	bool bUseWidestFrame,
	float AttackerHalfWidthOverride,
	float TargetHalfWidthOverride,
	float SideOverride)
{
	AActor* Attacker = IsValid(AttackerFlipbook) ? AttackerFlipbook->GetOwner() : nullptr;
	if (!IsValid(Attacker) || !IsValid(TargetFlipbook))
	{
		return IsValid(Attacker) ? Attacker->GetActorLocation() : FVector::ZeroVector;
	}

	const FGF_SpriteFootprint AttackerPrint = GetSpriteFootprint(AttackerFlipbook, bUseWidestFrame);
	const FGF_SpriteFootprint TargetPrint = GetSpriteFootprint(TargetFlipbook, bUseWidestFrame);
	if (!AttackerPrint.bValid || !TargetPrint.bValid)
	{
		// Staying put is a far better failure than leaping to the world origin.
		return Attacker->GetActorLocation();
	}

	const float AttackerHalf = (AttackerHalfWidthOverride > 0.0f) ? AttackerHalfWidthOverride : AttackerPrint.HalfWidth;
	const float TargetHalf = (TargetHalfWidthOverride > 0.0f) ? TargetHalfWidthOverride : TargetPrint.HalfWidth;

	// Both sprites are billboarded to the same camera yaw, so the target's right axis IS the
	// screen's left/right. Landing along it is what makes the gap read as a gap on screen; the raw
	// 3D direction between the two actors would fold in the depth between the rows and land short.
	const FVector Axis = TargetPrint.RightAxis;

	float Side = SideOverride;
	if (FMath::IsNearlyZero(Side))
	{
		const float Dot = static_cast<float>(FVector::DotProduct(AttackerPrint.Center - TargetPrint.Center, Axis));
		// Dead level with the target -- rare, and either side is as good -- so pick one rather
		// than take the sign of a near-zero and let it flip between frames.
		Side = (FMath::Abs(Dot) > KINDA_SMALL_NUMBER) ? FMath::Sign(Dot) : 1.0f;
	}
	Side = FMath::Sign(Side);

	FVector LandingFeet = TargetPrint.Feet + Axis * Side * (TargetHalf + AttackerHalf + Gap);

	if (!bMatchTargetGround)
	{
		LandingFeet.Z = AttackerPrint.Feet.Z;
	}

	if (!FMath::IsNearlyZero(DepthBias))
	{
		LandingFeet += HorizontalToCamera(TargetFlipbook, TargetPrint.Feet) * DepthBias;
	}

	// Move the ACTOR by however far its feet have to travel. The flipbook hangs off a pivot some
	// way below the root and every species hangs it differently, so placing the actor at the
	// landing spot directly would leave a tall creature floating and a small one buried.
	return Attacker->GetActorLocation() + (LandingFeet - AttackerPrint.Feet);
}

FVector UGF_MeleeStagingLibrary::GetLeapArcLocation(FVector Start, FVector End, float Alpha, float ArcHeight)
{
	const float T = FMath::Clamp(Alpha, 0.0f, 1.0f);

	// 4t(1-t) peaks at exactly 1 when t is 0.5, so ArcHeight is the height of the hop in world
	// units and not a number that has to be re-tuned every time the leap distance changes.
	const float Height = ArcHeight * 4.0f * T * (1.0f - T);

	return FMath::Lerp(Start, End, T) + FVector(0.0, 0.0, Height);
}
