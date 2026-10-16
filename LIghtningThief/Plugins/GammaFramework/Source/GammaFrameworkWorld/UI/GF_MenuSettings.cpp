#include "UI/GF_MenuSettings.h"

UGF_MenuSettings::UGF_MenuSettings()
{
	// The keys that commit something. Directions are deliberately absent.
	DampedKeys = {
		EKeys::Enter,
		EKeys::SpaceBar,
		EKeys::LeftMouseButton,
		EKeys::Gamepad_FaceButton_Bottom,
		EKeys::Gamepad_FaceButton_Right,
	};
}

const UGF_MenuSettings* UGF_MenuSettings::Get()
{
	return GetDefault<UGF_MenuSettings>();
}
