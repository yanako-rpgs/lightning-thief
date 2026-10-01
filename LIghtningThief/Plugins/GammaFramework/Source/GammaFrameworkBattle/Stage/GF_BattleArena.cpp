#include "GF_BattleArena.h"

#include "GF_BattleAnchor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/BillboardComponent.h"
#include "EngineUtils.h"

AGF_BattleArena::AGF_BattleArena()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("BattleCamera"));
	Camera->SetupAttachment(Root);

#if WITH_EDITORONLY_DATA
	Billboard = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Billboard"));
	if (Billboard)
	{
		Billboard->SetupAttachment(Root);
		Billboard->bIsScreenSizeScaled = true;
	}
#endif
}

AActor* AGF_BattleArena::GetViewTarget() const
{
	// The arena itself, unless an override was given: it carries a camera
	// component, and the view target uses that without being told.
	return CameraOverride ? static_cast<AActor*>(CameraOverride) : const_cast<AGF_BattleArena*>(this);
}

FTransform AGF_BattleArena::GetSeatTransform(EGF_BattleSide Side, int32 Index) const
{
	// Only anchors in THIS arena's level count. An arena streamed in beside
	// another one would otherwise pick up its neighbour's seats and put half a
	// battle in the wrong place.
	const ULevel* MyLevel = GetLevel();

	for (TActorIterator<AGF_BattleAnchor> It(GetWorld()); It; ++It)
	{
		const AGF_BattleAnchor* Anchor = *It;
		if (!Anchor || Anchor->GetLevel() != MyLevel) { continue; }

		if (Anchor->Side == Side && Anchor->Index == Index)
		{
			return Anchor->GetActorTransform();
		}
	}

	// Nothing placed for this seat. Worth saying out loud: an arena that looks
	// fully anchored in the editor but whose anchors sit in a different level
	// falls back for every seat, and the symptom is creatures clustered around
	// the arena origin rather than anywhere they were placed.
	int32 AnchorsInThisLevel = 0;
	for (TActorIterator<AGF_BattleAnchor> It(GetWorld()); It; ++It)
	{
		if (It->GetLevel() == MyLevel) { ++AnchorsInThisLevel; }
	}

	UE_LOG(LogTemp, Warning,
		TEXT("BattleArena '%s': no anchor for %s seat %d, using the default wedge. ")
		TEXT("%d anchor(s) are in this arena's level -- anchors in any other level are ignored."),
		*GetName(),
		Side == EGF_BattleSide::Player ? TEXT("player") : TEXT("enemy"),
		Index, AnchorsInThisLevel);

	FTransform Fallback = AGF_BattleAnchor::DefaultSlotTransform(Side, Index);
	Fallback.SetLocation(GetActorLocation() + Fallback.GetLocation());
	return Fallback;
}
