// Gamma Framework -- menu helpers that work on any widget.
//
// Deliberately a function library rather than a base widget class. A pause
// menu, a HUD, a shop and a dialogue box have little worth inheriting from one
// another, and a base class per kind is the same authoring problem in a
// different shape. These are static: any widget of any class can call them.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GF_MenuLibrary.generated.h"

class UUserWidget;
class UWidgetAnimation;
class UUMGSequencePlayer;

UCLASS()
class GAMMAFRAMEWORKWORLD_API UGF_MenuLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Play a widget animation and refuse menu changes until it finishes.
	 *
	 * Use this instead of Play Animation on anything that opens, closes or
	 * transitions. The block is the animation's own length, so retiming the
	 * animation retimes the guard -- nobody has to remember a number, and a
	 * press landing halfway through a half-second intro is absorbed rather than
	 * opening a second menu on top of one still arriving.
	 */
	UFUNCTION(BlueprintCallable, Category = "Gamma Framework|Menu",
	          meta = (DefaultToSelf = "Widget", AdvancedDisplay = "StartAtTime,NumLoops,PlayMode,PlaybackSpeed"))
	static UUMGSequencePlayer* PlayGuardedAnimation(UUserWidget* Widget,
	                                                UWidgetAnimation* Animation,
	                                                float StartAtTime = 0.f,
	                                                int32 NumLoops = 1,
	                                                float PlaybackSpeed = 1.f);

	/**
	 * Block menu changes for an explicit length, without playing anything.
	 *
	 * For a transition that is not a widget animation -- a camera move, a
	 * shader wipe, a level streaming in.
	 */
	UFUNCTION(BlueprintCallable, Category = "Gamma Framework|Menu",
	          meta = (WorldContext = "WorldContextObject"))
	static void BlockMenuInput(const UObject* WorldContextObject, float Seconds);

	/**
	 * Would a menu change be accepted right now?
	 *
	 * Bind a button's IsEnabled to this. A refused press with no feedback reads
	 * as a dropped input, which is what makes people press harder.
	 */
	UFUNCTION(BlueprintPure, Category = "Gamma Framework|Menu",
	          meta = (WorldContext = "WorldContextObject"))
	static bool IsMenuInputAllowed(const UObject* WorldContextObject);
};
