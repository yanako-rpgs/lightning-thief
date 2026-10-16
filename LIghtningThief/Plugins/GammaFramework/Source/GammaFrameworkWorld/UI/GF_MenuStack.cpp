#include "UI/GF_MenuStack.h"

#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UI/GF_InputDamper.h"
#include "UI/GF_MenuSettings.h"
#include "Framework/Application/SlateApplication.h"

void UGF_MenuStack::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (!FSlateApplication::IsInitialized()) { return; }

	Damper = MakeShared<FGF_InputDamper>();
	// From Project Settings -> Game -> Gamma Framework Menu, which is somewhere a
	// person can actually find and change it. A subsystem has no Details panel.
	const UGF_MenuSettings* Settings = UGF_MenuSettings::Get();
	Damper->Cooldown = Settings->MenuInputCooldown;
	Damper->DampedKeys.Append(Settings->DampedKeys);

	// Index 0: ahead of everything, including UINavigation. A press that is too
	// soon should not reach anything at all.
	FSlateApplication::Get().RegisterInputPreProcessor(Damper, 0);
}

void UGF_MenuStack::Deinitialize()
{
	if (Damper.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(Damper);
	}
	Damper.Reset();

	Super::Deinitialize();
}

void UGF_MenuStack::ResetToBootState()
{
	PopAll();

	// A block left over from a menu that no longer exists would swallow the
	// first press of the next run.
	if (Damper.IsValid())
	{
		Damper->ClearBlock();
	}
}

bool UGF_MenuStack::IsTransitioning() const
{
	return Damper.IsValid() && Damper->IsBlocked();
}

void UGF_MenuStack::BlockFor(float Seconds)
{
	if (Damper.IsValid())
	{
		Damper->BlockFor(Seconds);
	}
}

UUserWidget* UGF_MenuStack::PushMenu(TSubclassOf<UUserWidget> MenuClass, int32 ZOrder)
{
	if (!MenuClass || IsTransitioning()) { return nullptr; }

	UGameInstance* GI = GetGameInstance();
	if (!GI) { return nullptr; }

	UUserWidget* Menu = CreateWidget<UUserWidget>(GI, MenuClass);
	if (!Menu) { return nullptr; }

	Menu->AddToViewport(ZOrder);
	Stack.Add(Menu);

	// The floor. An animation that wants longer says so through BlockFor.
	BlockFor(0.f);

	return Menu;
}

bool UGF_MenuStack::PopMenu()
{
	if (Stack.Num() == 0 || IsTransitioning()) { return false; }

	if (UUserWidget* Top = Stack.Pop())
	{
		Top->RemoveFromParent();
	}

	BlockFor(0.f);
	return true;
}

void UGF_MenuStack::PopAll()
{
	// No transition check and no gap: this is for a battle ending or a scene
	// change, where the menus are going regardless of what was animating.
	for (UUserWidget* Menu : Stack)
	{
		if (Menu) { Menu->RemoveFromParent(); }
	}
	Stack.Reset();
}

UUserWidget* UGF_MenuStack::GetTopMenu() const
{
	return Stack.Num() > 0 ? Stack.Last() : nullptr;
}
