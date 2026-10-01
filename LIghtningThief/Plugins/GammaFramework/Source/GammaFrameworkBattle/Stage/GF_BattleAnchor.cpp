#include "GF_BattleAnchor.h"

#include "Components/BillboardComponent.h"
#include "Components/ArrowComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"

AGF_BattleAnchor::AGF_BattleAnchor()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

#if WITH_EDITORONLY_DATA
	Billboard = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Billboard"));
	if (Billboard)
	{
		Billboard->SetupAttachment(Root);
		Billboard->bIsScreenSizeScaled = true;
	}

	// The arrow points the way the creature faces, which is the one thing a
	// location alone cannot tell you when the two sides look at each other.
	Arrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	if (Arrow)
	{
		Arrow->SetupAttachment(Root);
		Arrow->ArrowSize = 1.5f;
		Arrow->bTreatAsASprite = true;
	}
#endif
}

#if WITH_EDITOR
void AGF_BattleAnchor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Colour by side so a field of eight anchors is readable at a glance
	// without clicking each one to read its properties.
	if (Arrow)
	{
		Arrow->SetArrowColor(Side == EGF_BattleSide::Player
			? FLinearColor(0.15f, 0.55f, 1.0f)
			: FLinearColor(1.0f, 0.25f, 0.2f));
	}
}
#endif

FTransform AGF_BattleAnchor::DefaultSlotTransform(EGF_BattleSide InSide, int32 InIndex)
{
	// X separates the sides -- player left, enemy right -- and Y is depth into
	// the scene. Matches the axes the test command has been spawning on.
	const float SideSign = (InSide == EGF_BattleSide::Player) ? -1.f : 1.f;

	// Offsets from the side's front seat. Each rank steps back from the enemy
	// and alternates in depth, so nothing hides directly behind the creature in
	// front of it.
	static const FVector2D Wedge[4] =
	{
		FVector2D(   0.f,    0.f),   // front
		FVector2D( 110.f,  130.f),
		FVector2D( 210.f, -110.f),
		FVector2D( 320.f,   45.f),
	};

	const int32 Clamped = FMath::Clamp(InIndex, 0, 3);
	const FVector2D& Offset = Wedge[Clamped];

	const FVector Location(
		SideSign * (300.f + Offset.X),
		Offset.Y,
		0.f);

	return FTransform(FRotator::ZeroRotator, Location, FVector::OneVector);
}

FTransform AGF_BattleAnchor::GetSlotTransform(const UObject* WorldContextObject,
                                              EGF_BattleSide InSide, int32 InIndex,
                                              bool& bOutFromAnchor)
{
	bOutFromAnchor = false;

	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	if (World)
	{
		for (TActorIterator<AGF_BattleAnchor> It(const_cast<UWorld*>(World)); It; ++It)
		{
			const AGF_BattleAnchor* Anchor = *It;
			if (Anchor && Anchor->Side == InSide && Anchor->Index == InIndex)
			{
				bOutFromAnchor = true;
				return Anchor->GetActorTransform();
			}
		}
	}

	return DefaultSlotTransform(InSide, InIndex);
}
