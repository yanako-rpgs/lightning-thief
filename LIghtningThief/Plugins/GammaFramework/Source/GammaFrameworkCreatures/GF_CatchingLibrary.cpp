// CatchingLibrary.cpp
#include "GF_CatchingLibrary.h"
#include "GF_ElementTypes.h"
#include "GF_CreatureSpeciesData.h"
#include "Math/UnrealMathUtility.h"

FGF_CatchResult UGF_CatchingLibrary::CalculateCatchAttempt(
	const FGF_CreatureInstanceData& WildCreature,
	UGF_ItemData* CoreData,
	const FGF_CatchContext& Context,
	float StatusModifier,
	int32 CompendiumCaught
)
{
	FGF_CatchResult Result;

	if (!CoreData || !WildCreature.SpeciesData)
	{
		UE_LOG(LogTemp, Error, TEXT("CatchingLibrary: Invalid Creature or Core data!"));
		return Result;
	}

	// Absolute Core always catches
	if (CoreData->CatchRateModifier >= 255.0f)
	{
		Result.bCaught = true;
		Result.bCriticalCapture = true;
		Result.ShakeCount = 3;
		Result.CatchProbability = 1.0f;
		Result.ModifiedCatchRate = 255.0f;

		UE_LOG(LogTemp, Log, TEXT("CatchingLibrary: Absolute Core used - Guaranteed catch!"));
		return Result;
	}

	// Get base catch rate from species (use your species data structure)
	float BaseCatchRate = WildCreature.SpeciesData->CatchRate;

	// Calculate HP modifier: (3 * MaxHP - 2 * CurrentHP) / (3 * MaxHP)
	float HPModifier = CalculateHPModifier(WildCreature);

	// Get core modifier (handles the conditional cores)
	float CoreModifier = GetCoreModifier(CoreData, WildCreature, Context);

	// Calculate modified catch rate
	float ModifiedCatchRate = BaseCatchRate * CoreModifier * StatusModifier * HPModifier;
	ModifiedCatchRate = ClampCatchRate(ModifiedCatchRate);

	Result.ModifiedCatchRate = ModifiedCatchRate;
	Result.CatchProbability = FMath::Clamp(ModifiedCatchRate / 255.0f, 0.0f, 1.0f);

	// Check for critical capture (classic+)
	if (CheckCriticalCapture(CompendiumCaught, ModifiedCatchRate))
	{
		Result.bCriticalCapture = true;
		Result.bCaught = true;
		Result.ShakeCount = 3;

		UE_LOG(LogTemp, Log, TEXT("CatchingLibrary: Critical Capture! %s caught instantly!"),
			*WildCreature.GetDisplayName().ToString());
		return Result;
	}

	// Calculate shake probability
	int32 ShakeProbability = CalculateShakeProbability(ModifiedCatchRate);

	// Perform shake checks (up to 4)
	int32 ShakeCount = CalculateShakeCount(ShakeProbability);
	Result.ShakeCount = ShakeCount;

	// Caught if all 4 shakes succeeded
	Result.bCaught = (ShakeCount >= 4);

	UE_LOG(LogTemp, Log, TEXT("CatchingLibrary: Catch attempt - Rate: %.2f, Shakes: %d, Caught: %s"),
		ModifiedCatchRate, ShakeCount, Result.bCaught ? TEXT("YES") : TEXT("NO"));

	return Result;
}

float UGF_CatchingLibrary::GetCatchProbabilityPercent(
	const FGF_CreatureInstanceData& WildCreature,
	UGF_ItemData* CoreData,
	const FGF_CatchContext& Context,
	float StatusModifier
)
{
	if (!CoreData || !WildCreature.SpeciesData)
		return 0.0f;

	// Absolute Core = 100%
	if (CoreData->CatchRateModifier >= 255.0f)
		return 100.0f;

	float BaseCatchRate = WildCreature.SpeciesData->CatchRate;
	float HPModifier = CalculateHPModifier(WildCreature);
	float CoreModifier = GetCoreModifier(CoreData, WildCreature, Context);

	float ModifiedCatchRate = BaseCatchRate * CoreModifier * StatusModifier * HPModifier;
	ModifiedCatchRate = ClampCatchRate(ModifiedCatchRate);

	// Calculate actual catch probability (all 4 shakes succeed)
	int32 ShakeProbability = CalculateShakeProbability(ModifiedCatchRate);
	float ShakeChance = FMath::Clamp(ShakeProbability / 65535.0f, 0.0f, 1.0f);

	// Probability of 4 successful shakes in a row
	float CatchChance = FMath::Pow(ShakeChance, 4.0f);

	return CatchChance * 100.0f;
}

bool UGF_CatchingLibrary::IsCreatureCaught(
	const FGF_CreatureInstanceData& WildCreature,
	UGF_ItemData* CoreData,
	const FGF_CatchContext& Context,
	float StatusModifier
)
{
	FGF_CatchResult Result = CalculateCatchAttempt(WildCreature, CoreData, Context, StatusModifier, 0);
	return Result.bCaught;
}

//--------------------
// CATCH RATE MODIFIERS
//--------------------

float UGF_CatchingLibrary::GetCoreModifier(
	UGF_ItemData* CoreData,
	const FGF_CreatureInstanceData& WildCreature,
	const FGF_CatchContext& Context
)
{
	if (!CoreData)
		return 1.0f;

	// Conditional cores below return their multiplier outright and never touch
	// the asset value, so only the flat cores fall through to BaseModifier.
	float BaseModifier = CoreData->CatchRateModifier;

	switch (CoreData->CoreType)
	{
		case EGF_CoreType::SnareCore:
		{
			// 3.5x on Bug or Water types
			if (UGF_CreatureSpeciesData* Species = WildCreature.SpeciesData.LoadSynchronous())
			{
				const bool bBugOrWater =
					Species->PrimaryElement   == EGF_Element::Chitin || Species->PrimaryElement   == EGF_Element::Tide ||
					Species->SecondaryElement == EGF_Element::Chitin || Species->SecondaryElement == EGF_Element::Tide;

				if (bBugOrWater)
					return 3.5f;
			}
			return 1.0f;
		}

		case EGF_CoreType::DepthCore:
			// 3.5x while surfing, fishing or underwater
			return Context.bIsUnderwater ? 3.5f : 1.0f;

		case EGF_CoreType::CradleCore:
			// Better the weaker the target: (41 - Level) / 10, never worse than a Core.
			// Caps at 4.0x against a level 1 Creature.
			return FMath::Clamp((41.0f - WildCreature.Level) / 10.0f, 1.0f, 4.0f);

		case EGF_CoreType::EchoCore:
			// 3.5x if this species is already registered as caught in the Compendium
			return Context.bSpeciesAlreadyCaught ? 3.5f : 1.0f;

		case EGF_CoreType::AeonCore:
		{
			// Ramps from 1.0x on turn 1 up to 4.0x from turn 11 onwards.
			// TurnCount 0 means the battle never told us, so give no bonus.
			const int32 TurnsPassed = FMath::Max(0, Context.TurnCount - 1);
			return FMath::Clamp(1.0f + 0.3f * TurnsPassed, 1.0f, 4.0f);
		}

		case EGF_CoreType::GloamCore:
			// 3.5x in caves or at night
			return Context.bIsNightOrCave ? 3.5f : 1.0f;

		case EGF_CoreType::SuddenCore:
			// 5.0x on the first turn of the battle only
			return (Context.TurnCount == 1) ? 5.0f : 1.0f;

		case EGF_CoreType::OmenCore:
			// 3.5x on a unique target, plain Core odds on anything else
			return WildCreature.bIsUnique ? 3.5f : 1.0f;

		default:
			// Simple/Greater/Hyper/Absolute/Warden/Hearth/Elite/Mend/Boon take the asset value
			break;
	}

	return BaseModifier;
}

float UGF_CatchingLibrary::CalculateHPModifier(const FGF_CreatureInstanceData& WildCreature)
{
	if (WildCreature.MaxHP <= 0)
		return 1.0f;

	float CurrentHP = FMath::Max(WildCreature.CurrentHP, 1.0f);
	float MaxHP = WildCreature.MaxHP;

	// classic-5 Formula: (3 * MaxHP - 2 * CurrentHP) / (3 * MaxHP)
	float Modifier = (3.0f * MaxHP - 2.0f * CurrentHP) / (3.0f * MaxHP);

	return FMath::Clamp(Modifier, 0.1f, 1.0f);
}

float UGF_CatchingLibrary::GetStatusModifier(uint8 StatusType)
{
	// TODO: Replace with your actual status enum values
	// Example mappings:
	// 0 = None (1.0x)
	// 1 = Sleep/Freeze (2.5x)
	// 2 = Paralysis/Burn/Poison (1.5x)

	if (StatusType == 0)
		return 1.0f;  // No status
	else if (StatusType == 1)
		return 2.5f;  // Sleep or Freeze
	else if (StatusType >= 2)
		return 1.5f;  // Paralysis, Burn, or Poison

	return 1.0f;
}

//--------------------
// CORE IDENTITY
//--------------------

bool UGF_CatchingLibrary::IsAbsoluteCore(EGF_CoreType CoreType)
{
	return CoreType == EGF_CoreType::AbsoluteCore;
}

FText UGF_CatchingLibrary::GetCoreName(EGF_CoreType CoreType)
{
	switch (CoreType)
	{
		case EGF_CoreType::SimpleCore:   return FText::FromString(TEXT("Simple Core"));
		case EGF_CoreType::GreaterCore:  return FText::FromString(TEXT("Greater Core"));
		case EGF_CoreType::HyperCore:    return FText::FromString(TEXT("Hyper Core"));
		case EGF_CoreType::AbsoluteCore: return FText::FromString(TEXT("Absolute Core"));
		case EGF_CoreType::WardenCore:   return FText::FromString(TEXT("Warden Core"));
		case EGF_CoreType::SnareCore:    return FText::FromString(TEXT("Snare Core"));
		case EGF_CoreType::DepthCore:    return FText::FromString(TEXT("Depth Core"));
		case EGF_CoreType::CradleCore:   return FText::FromString(TEXT("Cradle Core"));
		case EGF_CoreType::EchoCore:     return FText::FromString(TEXT("Echo Core"));
		case EGF_CoreType::AeonCore:     return FText::FromString(TEXT("Aeon Core"));
		case EGF_CoreType::HearthCore:   return FText::FromString(TEXT("Hearth Core"));
		case EGF_CoreType::EliteCore:    return FText::FromString(TEXT("Elite Core"));
		case EGF_CoreType::GloamCore:    return FText::FromString(TEXT("Gloam Core"));
		case EGF_CoreType::MendCore:     return FText::FromString(TEXT("Mend Core"));
		case EGF_CoreType::SuddenCore:   return FText::FromString(TEXT("Sudden Core"));
		case EGF_CoreType::BoonCore:     return FText::FromString(TEXT("Boon Core"));
		case EGF_CoreType::OmenCore:     return FText::FromString(TEXT("Omen Core"));
		default:                         return FText::FromString(TEXT("Unknown Core"));
	}
}

//--------------------
// HELPER FUNCTIONS
//--------------------

int32 UGF_CatchingLibrary::CalculateShakeProbability(float ModifiedCatchRate)
{
	if (ModifiedCatchRate <= 0)
		return 0;

	// classic-5 Formula: 65536 / sqrt(sqrt(255 / ModifiedCatchRate))
	// Equivalent to: 65536 / (255 / ModifiedCatchRate)^0.25

	float Ratio = 255.0f / ModifiedCatchRate;
	float FourthRoot = FMath::Pow(Ratio, 0.25f);
	int32 Probability = FMath::RoundToInt(65536.0f / FourthRoot);

	return FMath::Clamp(Probability, 0, 65535);
}

bool UGF_CatchingLibrary::CheckCriticalCapture(int32 CompendiumCaught, float CatchRate)
{
	// Critical capture chance increases with Compendium completion
	// classic formula approximation:
	// < 30 caught: No critical captures
	// 30-149: Small chance
	// 150-299: Medium chance
	// 300-449: High chance
	// 450+: Very high chance

	if (CompendiumCaught < 30)
		return false;

	float CriticalChance = 0.0f;

	if (CompendiumCaught >= 600)
		CriticalChance = 0.15f;  // 15%
	else if (CompendiumCaught >= 450)
		CriticalChance = 0.12f;  // 12%
	else if (CompendiumCaught >= 300)
		CriticalChance = 0.08f;  // 8%
	else if (CompendiumCaught >= 150)
		CriticalChance = 0.05f;  // 5%
	else if (CompendiumCaught >= 30)
		CriticalChance = 0.02f;  // 2%

	// Modified by catch rate (easier catches have higher crit chance)
	CriticalChance *= FMath::Clamp(CatchRate / 100.0f, 0.5f, 2.0f);

	return FMath::FRand() < CriticalChance;
}

float UGF_CatchingLibrary::ClampCatchRate(float CatchRate)
{
	// Catch rate must be between 0 and 255
	return FMath::Clamp(CatchRate, 0.0f, 255.0f);
}

float UGF_CatchingLibrary::GetClaimEXPMultiplier(int32 BaseCatchRate)
{
	// A rate of 0 is uncatchable, so nothing should ever reach here with one --
	// but the division has to be safe, and 1.0x is the harmless answer.
	if (BaseCatchRate <= 0)
	{
		return 1.0f;
	}

	// Above 255 is out of range for a catch rate; clamping keeps the multiplier
	// from dropping below parity with a KO, which would make claiming a common
	// Creature worse than downing it.
	const float Rate = FMath::Clamp(static_cast<float>(BaseCatchRate), 1.0f, 255.0f);

	return FMath::Pow(255.0f / Rate, 0.25f);
}

float UGF_CatchingLibrary::GetClaimEXPMultiplierForCreature(const FGF_CreatureInstanceData& Creature)
{
	if (Creature.SpeciesData.IsNull())
	{
		return 1.0f;
	}

	const UGF_CreatureSpeciesData* Species = Creature.SpeciesData.LoadSynchronous();
	if (!Species)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("GetClaimEXPMultiplierForCreature: species would not load; claim pays KO rate."));
		return 1.0f;
	}

	return GetClaimEXPMultiplier(Species->CatchRate);
}

int32 UGF_CatchingLibrary::GenerateShakeCheck()
{
	// Generate random number 0-65535
	return FMath::RandRange(0, 65535);
}

int32 UGF_CatchingLibrary::CalculateShakeCount(int32 ShakeProbability)
{
	// Perform up to 4 shake checks
	// Creature breaks free on first failed check

	for (int32 i = 0; i < 4; i++)
	{
		int32 Check = GenerateShakeCheck();

		if (Check >= ShakeProbability)
		{
			// Failed shake check - Creature breaks free
			return i;
		}
	}

	// All 4 checks passed - Creature caught!
	return 4;
}