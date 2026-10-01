#pragma once

#include "CoreMinimal.h"
#include "Components/RichTextBlockDecorator.h"
#include "GF_DialogueTextEffectDecorator.generated.h"

class UDataTable;

/**
 * Rich text decorator that animates individual characters within effect tags.
 * Supports: <Shake>, <Tremble>, <Wave>, <Glitch>, <Pulse>
 * Compound tags also work: <Shake.Bold>, <Wave.Italic.Orange>, etc.
 *
 * Setup:
 * 1. Add this to your dialogue widget's RichTextBlock "Decorator Classes" array
 *    (create a Blueprint subclass if you want to tweak defaults in-editor)
 * 2. Assign your style DataTable to DialogueStyleSet for color resolution
 * 3. Use tags in dialogue text: "Look at this <Wave>amazing</> text!"
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "GE Dialogue Text Effect Decorator"))
class GAMMAFRAMEWORKWORLD_API UGF_DialogueTextEffectDecorator : public URichTextBlockDecorator
{
	GENERATED_BODY()

public:
	UGF_DialogueTextEffectDecorator(const FObjectInitializer& ObjectInitializer);

	virtual TSharedPtr<ITextDecorator> CreateDecorator(URichTextBlock* InOwner) override;

	// =========================================================================
	// Shake — random position jitter, re-randomized at intervals
	// =========================================================================

	/** Max pixel offset per character. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects|Shake")
	float ShakeIntensity = 2.0f;

	/** Seconds between shake re-rolls. Lower = faster shaking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects|Shake")
	float ShakeInterval = 0.06f;

	// =========================================================================
	// Tremble — smaller, continuous jitter (every frame)
	// =========================================================================

	/** Max pixel offset per character (applied every frame). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects|Tremble")
	float TrembleIntensity = 1.0f;

	// =========================================================================
	// Wave — sinusoidal vertical movement
	// =========================================================================

	/** Vertical amplitude in pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects|Wave")
	float WaveAmplitude = 3.0f;

	/** Oscillation speed (higher = faster). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects|Wave")
	float WaveSpeed = 4.0f;

	/** Phase offset between adjacent characters (radians). Higher = more spread. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects|Wave")
	float WaveCharOffset = 0.5f;

	// =========================================================================
	// Glitch — random character replacement + position jitter
	// =========================================================================

	/** Max pixel offset during a glitch frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects|Glitch")
	float GlitchIntensity = 3.0f;

	/** Probability per character per frame of glitching (0-1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects|Glitch", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GlitchChance = 0.08f;

	// =========================================================================
	// Pulse — opacity oscillation
	// =========================================================================

	/** Cycles per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects|Pulse")
	float PulseSpeed = 2.0f;

	/** Minimum opacity at the bottom of the pulse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects|Pulse", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PulseMinOpacity = 0.3f;

	// =========================================================================
	// Color Lookup
	// =========================================================================

	/** Optional style DataTable override (FRichTextStyleRow rows).
	 *  Used to resolve named styles in compound tags like <Shake.Big> or <Wave.Orange>.
	 *  Leave empty — the decorator automatically falls back to the RichTextBlock's own
	 *  TextStyleSet, so you never need to set this manually. Only assign it if you want
	 *  the decorator to look up styles from a DIFFERENT table than the RichTextBlock uses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects|Style")
	TObjectPtr<UDataTable> DialogueStyleSet;
};
