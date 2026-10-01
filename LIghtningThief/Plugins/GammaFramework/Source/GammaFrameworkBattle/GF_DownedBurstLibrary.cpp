#include "GF_DownedBurstLibrary.h"

#include "Engine/Texture2D.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "PaperSprite.h"

namespace
{
	/**
	 * Finds the sprite a flipbook is showing at a given time, tolerating the awkward cases.
	 *
	 * A flipbook keyframe is allowed to hold a null sprite (that is how you author a blink or a
	 * gap), and the down animation will usually have run past its end by the time we burst. So
	 * clamp to the ends first, and if that still lands on a hole, walk outward for the nearest
	 * real frame rather than giving up -- an empty burst is a far worse failure than a burst
	 * built from the frame next door.
	 */
	UPaperSprite* ResolveSpriteAtTime(UPaperFlipbook* Flipbook, float Time)
	{
		if (!IsValid(Flipbook))
		{
			return nullptr;
		}

		if (UPaperSprite* Direct = Flipbook->GetSpriteAtTime(Time, /*bClampToEnds=*/true))
		{
			return Direct;
		}

		const int32 NumFrames = Flipbook->GetNumFrames();
		if (NumFrames <= 0)
		{
			return nullptr;
		}

		const float Duration = Flipbook->GetTotalDuration();
		const float Ratio = (Duration > KINDA_SMALL_NUMBER) ? FMath::Clamp(Time / Duration, 0.0f, 1.0f) : 0.0f;
		const int32 StartFrame = FMath::Clamp(FMath::FloorToInt(Ratio * NumFrames), 0, NumFrames - 1);

		for (int32 Offset = 0; Offset < NumFrames; ++Offset)
		{
			if (UPaperSprite* Before = Flipbook->GetSpriteAtFrame(StartFrame - Offset))
			{
				return Before;
			}
			if (UPaperSprite* After = Flipbook->GetSpriteAtFrame(StartFrame + Offset))
			{
				return After;
			}
		}

		return nullptr;
	}
}

bool UGF_DownBurstLibrary::GetFlipbookFrameSample(UPaperFlipbookComponent* FlipbookComponent, int32 MaxParticles, FGF_GEFlipbookFrameSample& OutSample)
{
	OutSample = FGF_GEFlipbookFrameSample();

	if (!IsValid(FlipbookComponent))
	{
		UE_LOG(LogTemp, Warning, TEXT("GF_DownBurst: no flipbook component supplied."));
		return false;
	}

	UPaperSprite* Sprite = ResolveSpriteAtTime(FlipbookComponent->GetFlipbook(), FlipbookComponent->GetPlaybackPosition());
	if (!IsValid(Sprite))
	{
		UE_LOG(LogTemp, Warning, TEXT("GF_DownBurst: '%s' has no sprite on the current frame."), *FlipbookComponent->GetName());
		return false;
	}

	UTexture2D* Texture = Sprite->GetBakedTexture();

	// BakedRenderData is the sprite's render triangles: XY is the quad position in local XZ
	// relative to the pivot, ZW is the normalised UV into the baked texture. Deliberately NOT
	// GetSourceUV()/GetSourceSize() -- those live behind WITH_EDITORONLY_DATA and vanish in a
	// cooked build, which would compile fine in the editor and then fail to package.
	const TArray<FVector4>& RenderData = Sprite->BakedRenderData;
	if (!IsValid(Texture) || RenderData.Num() < 3)
	{
		UE_LOG(LogTemp, Warning, TEXT("GF_DownBurst: sprite '%s' has no baked texture or render data."), *Sprite->GetName());
		return false;
	}

	FVector2D MinPos(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
	FVector2D MaxPos(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest());
	FVector2D MinUV(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
	FVector2D MaxUV(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest());

	for (const FVector4& Vertex : RenderData)
	{
		MinPos.X = FMath::Min(MinPos.X, Vertex.X);
		MinPos.Y = FMath::Min(MinPos.Y, Vertex.Y);
		MaxPos.X = FMath::Max(MaxPos.X, Vertex.X);
		MaxPos.Y = FMath::Max(MaxPos.Y, Vertex.Y);

		MinUV.X = FMath::Min(MinUV.X, Vertex.Z);
		MinUV.Y = FMath::Min(MinUV.Y, Vertex.W);
		MaxUV.X = FMath::Max(MaxUV.X, Vertex.Z);
		MaxUV.Y = FMath::Max(MaxUV.Y, Vertex.W);
	}

	OutSample.Texture = Texture;
	OutSample.UVMin = MinUV;
	OutSample.UVSize = MaxUV - MinUV;
	OutSample.LocalSize = MaxPos - MinPos;
	OutSample.LocalCenter = (MinPos + MaxPos) * 0.5;

	const int32 PixelsWide = FMath::Max(1, FMath::RoundToInt(OutSample.UVSize.X * Texture->GetSizeX()));
	const int32 PixelsHigh = FMath::Max(1, FMath::RoundToInt(OutSample.UVSize.Y * Texture->GetSizeY()));
	OutSample.FrameSizeInPixels = FIntPoint(PixelsWide, PixelsHigh);

	// Scale the grid down on BOTH axes by the same factor so the silhouette keeps its shape.
	// Dropping one axis alone would squash the Creature, which reads instantly on screen.
	double Scale = 1.0;
	if (MaxParticles > 0)
	{
		const double Total = static_cast<double>(PixelsWide) * static_cast<double>(PixelsHigh);
		if (Total > MaxParticles)
		{
			Scale = FMath::Sqrt(MaxParticles / Total);
		}
	}

	OutSample.GridResolution = FIntPoint(
		FMath::Max(1, FMath::RoundToInt(PixelsWide * Scale)),
		FMath::Max(1, FMath::RoundToInt(PixelsHigh * Scale)));

	OutSample.bValid = true;
	return true;
}

void UGF_DownBurstLibrary::ApplyFrameSampleToNiagara(UNiagaraComponent* NiagaraComponent, const FGF_GEFlipbookFrameSample& Sample)
{
	if (!IsValid(NiagaraComponent) || !Sample.bValid)
	{
		return;
	}

	// These names are passed through to Niagara verbatim -- neither SetTextureObject nor the
	// SetVariable* family prepends anything, so the "User." prefix has to be spelled out or the
	// lookup silently misses and the system runs on its defaults.
	UNiagaraFunctionLibrary::SetTextureObject(NiagaraComponent, TEXT("User.SpriteTexture"), Sample.Texture);
	NiagaraComponent->SetVariableVec2(TEXT("User.SpriteUVMin"), Sample.UVMin);
	NiagaraComponent->SetVariableVec2(TEXT("User.SpriteUVSize"), Sample.UVSize);
	NiagaraComponent->SetVariableVec2(TEXT("User.SpriteLocalSize"), Sample.LocalSize);
	NiagaraComponent->SetVariableVec2(TEXT("User.SpriteLocalCenter"), Sample.LocalCenter);
	NiagaraComponent->SetVariableVec2(TEXT("User.GridResolution"), FVector2D(Sample.GridResolution.X, Sample.GridResolution.Y));
}

UNiagaraComponent* UGF_DownBurstLibrary::SpawnFlipbookDownBurst(
	UPaperFlipbookComponent* FlipbookComponent,
	UNiagaraSystem* BurstSystem,
	FVector TargetWorldLocation,
	float BurstDuration,
	int32 MaxParticles,
	bool bHideFlipbook)
{
	if (!IsValid(BurstSystem))
	{
		UE_LOG(LogTemp, Warning, TEXT("GF_DownBurst: no Niagara system supplied."));
		return nullptr;
	}

	FGF_GEFlipbookFrameSample Sample;
	if (!GetFlipbookFrameSample(FlipbookComponent, MaxParticles, Sample))
	{
		return nullptr;
	}

	// bAutoActivate is false on purpose. Spawn-time modules read the user parameters once, when
	// the burst spawns, so activating before they are set would emit a grid built from whatever
	// defaults the system asset was saved with.
	UNiagaraComponent* Burst = UNiagaraFunctionLibrary::SpawnSystemAttached(
		BurstSystem,
		FlipbookComponent,
		NAME_None,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		EAttachLocation::SnapToTarget,
		/*bAutoDestroy=*/true,
		/*bAutoActivate=*/false);

	if (!IsValid(Burst))
	{
		UE_LOG(LogTemp, Warning, TEXT("GF_DownBurst: Niagara refused to spawn '%s'."), *BurstSystem->GetName());
		return nullptr;
	}

	ApplyFrameSampleToNiagara(Burst, Sample);
	Burst->SetVariableVec3(TEXT("User.TargetLocation"), TargetWorldLocation);
	Burst->SetVariableFloat(TEXT("User.BurstDuration"), BurstDuration);

	Burst->Activate();

	if (bHideFlipbook)
	{
		FlipbookComponent->SetVisibility(false, /*bPropagateToChildren=*/true);
	}

	UE_LOG(LogTemp, Log, TEXT("GF_DownBurst: '%s' burst from a %dx%d frame on a %dx%d grid (%d particles), converging on %s."),
		*FlipbookComponent->GetName(),
		Sample.FrameSizeInPixels.X, Sample.FrameSizeInPixels.Y,
		Sample.GridResolution.X, Sample.GridResolution.Y,
		Sample.GridResolution.X * Sample.GridResolution.Y,
		*TargetWorldLocation.ToCompactString());

	return Burst;
}
