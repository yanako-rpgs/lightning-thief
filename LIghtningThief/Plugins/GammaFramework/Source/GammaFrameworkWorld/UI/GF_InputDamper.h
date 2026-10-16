// Gamma Framework -- swallowing inputs that arrive too fast.
//
// Sits in front of Slate, so a press that comes too soon never reaches a
// widget at all. Nothing in the UI has to check anything, opt in, or be
// rebuilt: buttons, menus and UINavigation carry on as they are and simply
// never see the presses that were going to break them.
//
// The alternative was asking every button to test a flag, which is the same
// work spread across every widget in the game and forgotten on one of them.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"

class FGF_InputDamper : public IInputProcessor
{
public:
	/** Seconds a damped press blocks the next one. */
	float Cooldown = 0.15f;

	/**
	 * The keys this applies to. Everything else passes through untouched --
	 * navigation especially, which is walked by tapping a direction repeatedly
	 * and would feel broken if each tap were swallowed.
	 */
	TSet<FKey> DampedKeys;

	/** Swallow everything until this time. Extended by menu animations. */
	double BlockedUntil = 0.0;

	bool IsBlocked() const;

	/** Extend the block. Takes the later of the two, never shortens one. */
	void BlockFor(float Seconds);

	/** Drop any block in progress. For a reset, where the thing it protected is gone. */
	void ClearBlock() { BlockedUntil = 0.0; }

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp,
	                  TSharedRef<ICursor> Cursor) override {}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp,
	                                const FKeyEvent& InKeyEvent) override;

	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp,
	                                        const FPointerEvent& MouseEvent) override;

	virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp,
	                                    const FAnalogInputEvent& InAnalogInputEvent) override;
};
