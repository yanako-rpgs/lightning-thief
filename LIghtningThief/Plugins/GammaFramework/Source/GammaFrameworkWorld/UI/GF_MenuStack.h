// Gamma Framework -- the one thing that opens and closes menus.
//
// Two widgets on screen at once is not a timing bug, and no cooldown fixes it
// properly. It happens because any widget can open any other widget, so a
// spammed input eventually interleaves two of them -- a pause menu reopening
// while its own submenu is arriving, and now both exist.
//
// A widget no longer opens a widget. It asks the stack, and the stack is the
// only thing that adds or removes anything, so "one at a time" holds by
// construction rather than by everyone remembering to check.
//
// Not a base class: menus, a HUD, a shop and a dialogue box have nothing in
// common worth inheriting, and a base class per kind is the same authoring
// problem wearing a hat.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GF_ResettableState.h"
#include "GF_MenuStack.generated.h"

class UUserWidget;
class FGF_InputDamper;

UCLASS()
class GAMMAFRAMEWORKWORLD_API UGF_MenuStack : public UGameInstanceSubsystem, public IGF_ResettableState
{
	GENERATED_BODY()

public:
	/**
	 * Open a menu on top of whatever is already there.
	 *
	 * Returns the widget, or null if the stack is mid-transition and refused.
	 * The one below stays alive and on screen -- so a submenu can blur its
	 * parent rather than replace it -- it simply stops being the top.
	 */
	UFUNCTION(BlueprintCallable, Category = "Gamma Framework|Menu",
	          meta = (DeterminesOutputType = "MenuClass"))
	UUserWidget* PushMenu(TSubclassOf<UUserWidget> MenuClass, int32 ZOrder = 0);

	/** Close the top menu. Returns false when nothing is open, or mid-transition. */
	UFUNCTION(BlueprintCallable, Category = "Gamma Framework|Menu")
	bool PopMenu();

	/** Close everything. For a battle ending, or a scene change. */
	UFUNCTION(BlueprintCallable, Category = "Gamma Framework|Menu")
	void PopAll();

	UFUNCTION(BlueprintPure, Category = "Gamma Framework|Menu")
	UUserWidget* GetTopMenu() const;

	UFUNCTION(BlueprintPure, Category = "Gamma Framework|Menu")
	int32 GetMenuCount() const { return Stack.Num(); }

	/**
	 * Is a transition running?
	 *
	 * Push and Pop refuse while it is, which is what a spammed input runs into
	 * instead of a half-built menu.
	 */
	UFUNCTION(BlueprintPure, Category = "Gamma Framework|Menu")
	bool IsTransitioning() const;

	/**
	 * Refuse menu changes for this long.
	 *
	 * PlayGuardedAnimation calls this with an animation's own length, so the
	 * block lasts exactly as long as the thing it protects. Extends rather than
	 * replaces: two overlapping blocks end with the later of the two.
	 */
	UFUNCTION(BlueprintCallable, Category = "Gamma Framework|Menu")
	void BlockFor(float Seconds);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// IGF_ResettableState -- closes every menu and drops any block in progress.
	virtual void ResetToBootState() override;

private:
	UPROPERTY() TArray<TObjectPtr<UUserWidget>> Stack;

	// Sits in front of Slate and swallows presses that arrive too fast, so no
	// widget has to know this exists.
	TSharedPtr<FGF_InputDamper> Damper;
};
