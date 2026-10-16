#include "UI/GF_InputDamper.h"

#include "Framework/Application/SlateApplication.h"

bool FGF_InputDamper::IsBlocked() const
{
	return FSlateApplication::IsInitialized()
		&& FSlateApplication::Get().GetCurrentTime() < BlockedUntil;
}

void FGF_InputDamper::BlockFor(float Seconds)
{
	if (!FSlateApplication::IsInitialized()) { return; }

	BlockedUntil = FMath::Max(BlockedUntil,
		FSlateApplication::Get().GetCurrentTime() + FMath::Max(0.f, Seconds));
}

bool FGF_InputDamper::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	// Repeats are the key being held, not pressed again, and holding a
	// direction to scroll a list has to keep working.
	if (InKeyEvent.IsRepeat()) { return false; }

	// Only the keys that commit something. A direction is not one of them.
	if (!DampedKeys.Contains(InKeyEvent.GetKey())) { return false; }

	if (IsBlocked()) { return true; }

	BlockFor(Cooldown);
	return false;
}

bool FGF_InputDamper::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (!DampedKeys.Contains(MouseEvent.GetEffectingButton())) { return false; }

	if (IsBlocked()) { return true; }

	BlockFor(Cooldown);
	return false;
}

bool FGF_InputDamper::HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent)
{
	// Never. Sticks are how a list is navigated, and damping them would make
	// the menus feel broken rather than protected.
	return false;
}
