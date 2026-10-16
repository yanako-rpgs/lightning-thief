#include "UI/GF_MenuLibrary.h"

#include "UI/GF_MenuStack.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

namespace
{
	UGF_MenuStack* FindStack(const UObject* WorldContextObject)
	{
		const UWorld* World = GEngine
			? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
			: nullptr;

		UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UGF_MenuStack>() : nullptr;
	}
}

UUMGSequencePlayer* UGF_MenuLibrary::PlayGuardedAnimation(UUserWidget* Widget,
                                                          UWidgetAnimation* Animation,
                                                          float StartAtTime,
                                                          int32 NumLoops,
                                                          float PlaybackSpeed)
{
	if (!Widget || !Animation) { return nullptr; }

	// The animation's own length, adjusted for how fast it is being played and
	// how many times. Asking the animation is what keeps the guard correct when
	// someone retimes it later.
	const float Length = FMath::Max(0.f, Animation->GetEndTime() - StartAtTime);
	const float Speed  = FMath::Max(KINDA_SMALL_NUMBER, FMath::Abs(PlaybackSpeed));
	const float Loops  = static_cast<float>(FMath::Max(1, NumLoops));

	if (UGF_MenuStack* Stack = FindStack(Widget))
	{
		Stack->BlockFor((Length / Speed) * Loops);
	}

	return Widget->PlayAnimation(Animation, StartAtTime, NumLoops,
		EUMGSequencePlayMode::Forward, PlaybackSpeed);
}

void UGF_MenuLibrary::BlockMenuInput(const UObject* WorldContextObject, float Seconds)
{
	if (UGF_MenuStack* Stack = FindStack(WorldContextObject))
	{
		Stack->BlockFor(Seconds);
	}
}

bool UGF_MenuLibrary::IsMenuInputAllowed(const UObject* WorldContextObject)
{
	const UGF_MenuStack* Stack = FindStack(WorldContextObject);

	// No stack means nothing is guarding anything, so nothing is blocked. A
	// missing subsystem should not make every button in the game inert.
	return !Stack || !Stack->IsTransitioning();
}
