#include "GF_BattleResolver.h"

#include "GF_BattleBoard.h"
#include "GF_Creature.h"
#include "GF_CreatureRules.h"
#include "GF_CreatureStatStageComponent.h"
#include "GF_CreatureTraits.h"
#include "GF_FlinchHelperLibrary.h"
#include "GF_HeldItemBattleHelper.h"
#include "GF_SkillDefinition.h"
#include "GF_TurnOrderLibrary.h"

#define LOCTEXT_NAMESPACE "GF_BattleResolver"

DEFINE_LOG_CATEGORY_STATIC(LogGF_BattleResolve, Log, All);

//======================================================================================
// HELPERS
//======================================================================================

namespace
{
	/**
	 * A creature's name as the player should read it.
	 *
	 * Bare for your own -- you are playing them, and a qualifier on every line
	 * is noise. Marked for anything opposing you, because two of the same
	 * species on the field makes a bare name ambiguous about whose turn it is.
	 */
	FText QualifiedName(const AGF_Creature* Creature, const FText& DisplayName)
	{
		if (Creature == nullptr || Creature->isPlayerCreature)
		{
			return DisplayName;
		}

		return Creature->isWildCreature
			? FText::Format(LOCTEXT("QualifiedWild",  "The wild {0}"),  DisplayName)
			: FText::Format(LOCTEXT("QualifiedEnemy", "The enemy {0}"), DisplayName);
	}
}

void UGF_BattleResolver::GatherStatMultipliers(const AGF_Creature* Creature,
	float& OutAttack, float& OutDefense, float& OutMagic, float& OutPoise, float& OutSpeed)
{
	OutAttack = OutDefense = OutMagic = OutPoise = OutSpeed = 1.0f;

	if (Creature == nullptr)
	{
		return;
	}

	const UGF_CreatureStatStageComponent* Stages = Creature->FindComponentByClass<UGF_CreatureStatStageComponent>();
	if (Stages == nullptr)
	{
		// A creature with no stage component is simply at neutral stages. That is
		// a legitimate setup for a wild encounter, not a misconfiguration.
		return;
	}

	OutAttack  = Stages->GetStatMultiplier(EGF_StatStages::AttackUp);
	OutDefense = Stages->GetStatMultiplier(EGF_StatStages::DefenseUp);
	OutMagic   = Stages->GetStatMultiplier(EGF_StatStages::MagicUp);
	OutPoise   = Stages->GetStatMultiplier(EGF_StatStages::PoiseUp);
	OutSpeed   = Stages->GetStatMultiplier(EGF_StatStages::SpeedUp);
}

void UGF_BattleResolver::ApplyHPDelta(AGF_Creature* Creature, float Delta, float& OutHPAfter, bool& bOutDowned)
{
	if (Creature == nullptr)
	{
		OutHPAfter = 0.0f;
		bOutDowned = false;
		return;
	}

	const float OldHP = Creature->CurrentStats.CurrentHP;

	Creature->CurrentStats.CurrentHP = FMath::Clamp(
		Creature->CurrentStats.CurrentHP + Delta, 0.0f, Creature->CurrentStats.MaxHP);

	OutHPAfter = Creature->CurrentStats.CurrentHP;

	if (Creature->CurrentStats.CurrentHP <= 0.0f)
	{
		Creature->isDowned = true;
		bOutDowned = true;
	}
	else
	{
		bOutDowned = false;
	}

	// Battle damage does not travel through AActor::TakeDamage -- it is applied
	// here, directly. Without this call the presentation events on the creature
	// would never fire during a fight, which is the only place they matter.
	if (!FMath::IsNearlyEqual(OldHP, Creature->CurrentStats.CurrentHP))
	{
		if (Delta < 0.0f)
		{
			Creature->NotifyDamageTaken(-Delta, /*bWasCritical*/ false);
		}
		Creature->NotifyHealthChanged(OldHP, Creature->CurrentStats.CurrentHP);
	}
}

float UGF_BattleResolver::ApplyDamageModifiers(float RawDamage, const AGF_Creature* Target, int32 TargetCount,
	float MultiTargetMultiplier, bool& bOutSpreadDiscounted, bool& bOutBraced)
{
	bOutSpreadDiscounted = false;
	bOutBraced = false;

	float Damage = RawDamage;

	// The spread discount is what keeps focusing a target down competitive with
	// hitting everything. It applies on the real number of seats reached, so a
	// spread skill against a single surviving enemy pays no penalty.
	if (TargetCount > 1 && MultiTargetMultiplier > 0.0f && MultiTargetMultiplier < 1.0f)
	{
		Damage *= MultiTargetMultiplier;
		bOutSpreadDiscounted = true;
	}

	if (Target != nullptr && Target->bIsBracing)
	{
		Damage *= 0.5f;
		bOutBraced = true;
	}

	// Anything that connected takes at least 1. A resisted hit rounding to zero
	// reads as a bug to a player watching an unmoved health bar.
	return (RawDamage > 0.0f) ? FMath::Max(1.0f, Damage) : 0.0f;
}

bool UGF_BattleResolver::IsImmuneToWeatherChip(EGF_Element Element, EGF_WeatherType Weather)
{
	switch (Weather)
	{
	case EGF_WeatherType::Sandstorm:
		return false;   // no Rock/Steel equivalent among the Dokimon elements
	case EGF_WeatherType::Hail:
		return Element == EGF_Element::Ice;
	default:
		// Sun and rain do not chip anything.
		return true;
	}
}

//======================================================================================
// PLAN
//======================================================================================

FGF_ActionResolution UGF_BattleResolver::PlanAction(
	UGF_BattleComponent* DamageComponent,
	const UGF_BattleBoard* Board,
	const FGF_BattleAction& Action,
	EGF_WeatherType Weather)
{
	FGF_ActionResolution Resolution;
	Resolution.Actor = Action.Actor;
	Resolution.ActionType = Action.ActionType;
	Resolution.SkillClass = Action.SkillClass;

	if (Board == nullptr)
	{
		Resolution.bFailed = true;
		Resolution.FailMessage = LOCTEXT("NoBoard", "No battle board.");
		return Resolution;
	}

	AGF_Creature* Attacker = Board->GetCreatureInSlot(Action.Actor);
	Resolution.ActorCreature = Attacker;
	Resolution.ActorDisplayName = Board->GetDisplayNameForSlot(Action.Actor);
	Resolution.ActorHPBefore = Attacker ? Attacker->CurrentStats.CurrentHP : 0.0f;
	Resolution.ActorHPAfter = Resolution.ActorHPBefore;

	if (Attacker == nullptr || Attacker->IsDowned())
	{
		Resolution.bFailed = true;
		Resolution.FailMessage = LOCTEXT("ActorGone", "There is nobody in that slot.");
		return Resolution;
	}

	// A recharge turn swallows whatever was chosen. Checked before the action type
	// so a recharging creature cannot slip a Brace or an item in instead.
	if (Attacker->bMustRecharge)
	{
		Resolution.bFailed = true;
		Resolution.bRechargeTurn = true;
		Resolution.FailMessage = FText::Format(LOCTEXT("MustRecharge", "{0} must recharge!"),
			QualifiedName(Attacker, Resolution.ActorDisplayName));
		return Resolution;
	}

	// Everything that is not an attack resolves elsewhere. Brace is the one this
	// library owns outright, because it is purely a damage modifier.
	if (Action.ActionType != EGF_BattleActionType::Skill)
	{
		if (Action.ActionType == EGF_BattleActionType::Brace)
		{
			// Not applied here -- ApplyResolution does it, so a planned Brace that
			// is never committed leaves no stance behind.
			return Resolution;
		}

		// Item, Marque, Swap, Flee and Pass are sequenced by the flow and carried
		// out by the systems that own them. Nothing to compute.
		return Resolution;
	}

	if (DamageComponent == nullptr)
	{
		Resolution.bFailed = true;
		Resolution.FailMessage = LOCTEXT("NoDamageComponent", "No damage component supplied.");
		UE_LOG(LogGF_BattleResolve, Error, TEXT("PlanAction called without a UGF_BattleComponent."));
		return Resolution;
	}

	if (!Action.SkillClass)
	{
		Resolution.bFailed = true;
		Resolution.FailMessage = LOCTEXT("NoSkill", "No skill in that slot.");
		return Resolution;
	}

	const AGF_SkillDefinition* SkillCDO = Action.SkillClass.GetDefaultObject();
	if (SkillCDO == nullptr)
	{
		Resolution.bFailed = true;
		Resolution.FailMessage = LOCTEXT("BadSkill", "That skill could not be read.");
		return Resolution;
	}

	Resolution.SkillIndexToSpend = Action.SkillIndex;

	// Set by a faster creature's hit earlier this round and cleared when the
	// next round starts. Only a skill is stopped -- swaps, items and Brace
	// already returned above, matching the games, where a flinch costs the move.
	if (Attacker->bIsFlinched)
	{
		Resolution.bFailed = true;
		Resolution.bFlinchTurn = true;
		Resolution.FailMessage = FText::FromString(Attacker->GetFlinchMessage());
		return Resolution;
	}

	if (SkillCDO->bFirstTurnOnly && Attacker->bHasActedSinceEntering)
	{
		Resolution.bFailed = true;
		Resolution.FailMessage = LOCTEXT("NotFirstTurn", "But it failed!");
		return Resolution;
	}

	if (Action.Targets.Num() == 0)
	{
		Resolution.bFailed = true;
		Resolution.FailMessage = LOCTEXT("NoTargets", "There was no target.");
		return Resolution;
	}

	const FGF_CreatureInstanceData AttackerData = Attacker->ExportToInstanceData();

	float AtkA, AtkD, AtkM, AtkP, AtkS;
	GatherStatMultipliers(Attacker, AtkA, AtkD, AtkM, AtkP, AtkS);

	// Rolled once for the whole action: a multi-hit skill strikes the same number
	// of times on every target it reaches.
	const int32 HitCount = FMath::Max(1, UGF_BattleComponent::GetMultiHitCount(Action.SkillClass));

	// Count the seats that will actually be struck, not the seats aimed at. A
	// spread skill whose other targets are already down should not be discounted
	// for hitting them.
	int32 LiveTargetCount = 0;
	for (const FGF_BattleSlot& TargetSlot : Action.Targets)
	{
		if (Board->IsSlotLiving(TargetSlot))
		{
			++LiveTargetCount;
		}
	}

	for (const FGF_BattleSlot& TargetSlot : Action.Targets)
	{
		AGF_Creature* Defender = Board->GetCreatureInSlot(TargetSlot);
		if (Defender == nullptr || Defender->IsDowned())
		{
			continue;
		}

		FGF_TargetResolution TargetResult;
		TargetResult.Target = TargetSlot;
		TargetResult.Creature = Defender;
		TargetResult.DisplayName = Board->GetDisplayNameForSlot(TargetSlot);
		TargetResult.HPBefore = Defender->CurrentStats.CurrentHP;
		TargetResult.HPAfter = TargetResult.HPBefore;
		TargetResult.HitCount = HitCount;

		// The damage formula checks accuracy and evasion but not where the target
		// physically is, so mid-Fly and mid-Dig are caught here instead.
		if (Defender->SemiInvulnerableState != EGF_SemiInvulnerableState::None)
		{
			TargetResult.bSemiInvulnerable = true;
			TargetResult.Damage.bMissed = true;
			Resolution.Targets.Add(TargetResult);
			continue;
		}

		const FGF_CreatureInstanceData DefenderData = Defender->ExportToInstanceData();

		float DefA, DefD, DefM, DefP, DefS;
		GatherStatMultipliers(Defender, DefA, DefD, DefM, DefP, DefS);

		float TotalRaw = 0.0f;
		FGF_BattleDamageResult LastHit;

		for (int32 Hit = 0; Hit < HitCount; ++Hit)
		{
			// Each hit of a multi-hit skill rolls its own accuracy, crit and
			// damage spread, which is what makes them swingy by design.
			const FGF_BattleDamageResult HitResult = DamageComponent->CalculateDamageWithoutSpawning(
				AttackerData,
				DefenderData,
				Action.SkillClass,
				Attacker->HeldItem,
				Defender->HeldItem,
				Weather,
				/*bHasReflect*/ false,
				/*bHasLightScreen*/ false,
				AtkA, AtkD, AtkM, AtkP, AtkS,
				DefA, DefD, DefM, DefP, DefS,
				Attacker->CritStage + Attacker->CritStageBoost,
				/*BasePowerOverride*/ -1,
				// Always pass CurrentTrait: it equals the owned trait normally and
				// differs only after Trace, so passing it is never wrong.
				Attacker->CurrentTrait,
				Defender->CurrentTrait);

			LastHit = HitResult;
			TotalRaw += HitResult.Damage;

			if (HitResult.bMissed || HitResult.bImmune)
			{
				break;
			}
		}

		TargetResult.Damage = LastHit;
		TargetResult.Damage.Damage = TotalRaw;

		TargetResult.DamageToApply = ApplyDamageModifiers(
			TotalRaw, Defender, LiveTargetCount, SkillCDO->MultiTargetDamageMultiplier,
			TargetResult.bSpreadDiscounted, TargetResult.bReducedByBrace);

		// --- did it reach the target at all? --------------------------------
		if (Defender->bIsProtected && !SkillCDO->bIgnoresProtect)
		{
			TargetResult.bBlockedByProtect = true;
			TargetResult.Damage.bWasProtected = true;
			TargetResult.DamageToApply = 0.0f;
		}
		else if (TargetSlot == Action.Actor || SkillCDO->TargetShape == EGF_SkillTargetShape::Self)
		{
			// You do not roll accuracy against yourself. Without this, Recover and
			// Swords Dance can miss, which is both wrong and very hard to spot --
			// it looks like the heal simply did not fire.
			TargetResult.bConnected = true;
		}
		else if (!GF_IsDamagingCategory(SkillCDO->Split))
		{
			// A Utility or Healing skill returns from the damage formula before the accuracy
			// check ever runs -- it short-circuits on the non-damaging split. So the
			// roll has to happen here, or every such skill would land for free.
			TargetResult.bConnected = DamageComponent->CheckSkillHit(Action.SkillClass, AttackerData, DefenderData);
			TargetResult.Damage.bMissed = !TargetResult.bConnected;
		}
		else
		{
			TargetResult.bConnected = !TargetResult.Damage.bMissed && !TargetResult.Damage.bImmune;
		}

		// --- roll the secondary effects -------------------------------------
		// Rolled now, carried out in ApplyResolution. Rolling here is what makes
		// the plan final: the presentation can show a burn landing before the
		// burn is actually on the creature.
		if (TargetResult.bConnected)
		{
			if (SkillCDO->Status != EGF_STATUSEffect::None && SkillCDO->StatusChance > 0)
			{
				TargetResult.Status.Status = SkillCDO->Status;
				TargetResult.Status.bRolled = (FMath::RandRange(1, 100) <= SkillCDO->StatusChance);

				if (TargetResult.Status.bRolled)
				{
					FString FailMessage;
					const bool bAllowed = DamageComponent->CanStatusSkillAffect(
						Action.SkillClass, DefenderData, FailMessage);

					if (!bAllowed)
					{
						TargetResult.Status.bBlocked = true;
						TargetResult.Status.BlockMessage = FText::FromString(FailMessage);
					}
				}
			}

			if (UGF_FlinchHelperLibrary::CanSkillClassCauseFlinch(Action.SkillClass))
			{
				const int32 Chance = UGF_FlinchHelperLibrary::GetFlinchChanceFromSkillClass(Action.SkillClass);
				TargetResult.bFlinched = (Chance > 0) && (FMath::RandRange(1, 100) <= Chance);
			}

			// Self-targeting stat changes are handled once for the whole action
			// below. Applying them here as well would multiply a Swords Dance by
			// the number of seats the skill nominally reaches.
			if (SkillCDO->bAffectsStatStage && !SkillCDO->bStatChangeAffectsSelf
				&& FMath::RandRange(1, 100) <= SkillCDO->StatStageChance)
			{
				FGF_StatStageOutcome Planned;
				Planned.Stat = SkillCDO->bStatIsRandom
					? PickRandomStatStage(SkillCDO->StatStageAmount > 0)
					: SkillCDO->StatStages;
				Planned.Requested = SkillCDO->StatStageAmount;
				TargetResult.StatStages.Add(Planned);
			}

			// The list form: every target-side change lands on one roll per target.
			if (SkillCDO->StatChanges.ContainsByPredicate(
					[](const FGF_SkillStatChange& Change) { return !Change.bAffectsSelf && Change.Stages != 0; })
				&& FMath::RandRange(1, 100) <= SkillCDO->StatStageChance)
			{
				for (const FGF_SkillStatChange& Change : SkillCDO->StatChanges)
				{
					if (Change.bAffectsSelf || Change.Stages == 0)
					{
						continue;
					}

					FGF_StatStageOutcome Planned;
					Planned.Stat = Change.ToStatStage();
					Planned.Requested = Change.Stages;
					TargetResult.StatStages.Add(Planned);
				}
			}

			if (SkillCDO->bIsTrappingSkill)
			{
				TargetResult.bTrapped = true;
			}
		}

		Resolution.Targets.Add(TargetResult);
	}

	if (Resolution.Targets.Num() == 0)
	{
		Resolution.bFailed = true;
		Resolution.FailMessage = LOCTEXT("TargetsGone", "There was no target left.");
		return Resolution;
	}

	//----------------------------------------------------------------------------
	// Effects on the user, once per action rather than once per target.
	//----------------------------------------------------------------------------

	float TotalDamageDealt = 0.0f;
	bool bAnythingConnected = false;
	for (const FGF_TargetResolution& TargetResult : Resolution.Targets)
	{
		TotalDamageDealt += TargetResult.DamageToApply;
		bAnythingConnected |= TargetResult.bConnected;
	}

	if (SkillCDO->bAffectsStatStage && SkillCDO->bStatChangeAffectsSelf
		&& FMath::RandRange(1, 100) <= SkillCDO->StatStageChance)
	{
		FGF_StatStageOutcome Planned;
		Planned.Stat = SkillCDO->bStatIsRandom
			? PickRandomStatStage(SkillCDO->StatStageAmount > 0)
			: SkillCDO->StatStages;
		Planned.Requested = SkillCDO->StatStageAmount;
		Planned.bSelfInflicted = true;
		Resolution.SelfStatStages.Add(Planned);
	}

	if (SkillCDO->bHasRecoil && TotalDamageDealt > 0.0f)
	{
		Resolution.SelfDamage += UGF_CreatureTraitLibrary::GetRecoilDamage(
			Attacker->CurrentTrait, Action.SkillClass, TotalDamageDealt, Attacker->CurrentStats.MaxHP);
	}

	if (SkillCDO->bIsDrainingSkill && TotalDamageDealt > 0.0f)
	{
		const float Drain = TotalDamageDealt * SkillCDO->DrainPercentage;

		// Foulsap turns the drain around. Read it off the FIRST target that was
		// actually hit -- a spread drain against a mixed line is ambiguous, and
		// picking the primary target is the least surprising answer.
		bool bFoulsap = false;
		for (const FGF_TargetResolution& TargetResult : Resolution.Targets)
		{
			if (TargetResult.bConnected && TargetResult.Creature)
			{
				bFoulsap = (TargetResult.Creature->CurrentTrait == EGF_CreatureTrait::Foulsap);
				break;
			}
		}

		if (bFoulsap)
		{
			Resolution.SelfDamage += Drain;
			Resolution.bDrainBackfired = true;
		}
		else
		{
			Resolution.SelfHeal += Drain;
		}
	}

	if (SkillCDO->bIsSelfHealingSkill)
	{
		Resolution.SelfHeal += Attacker->CurrentStats.MaxHP * SkillCDO->SelfHealPercentage;
	}

	if (SkillCDO->bIsSelfKOSkill)
	{
		// Selfdestruct and Explosion. Enough to zero the user whatever its HP is,
		// and ApplyHPDelta clamps at zero.
		Resolution.SelfDamage += Attacker->CurrentStats.MaxHP;
	}

	Resolution.bCritStageRaised = SkillCDO->bRaisesCritStage;
	Resolution.bSelfStatusCleared = SkillCDO->bIsRefresh && Attacker->Status != EGF_STATUS::None;

	// Dokimon rules. A Utility or Healing skill's effects on the user always land; a damaging
	// skill's only land when it connects, so a missed Mighty Mash raises nothing
	// and a missed Dark Bond heals nothing.
	const bool bSelfEffectsLand = !GF_IsDamagingCategory(SkillCDO->Split) || bAnythingConnected;

	if (bSelfEffectsLand
		&& SkillCDO->StatChanges.ContainsByPredicate(
			[](const FGF_SkillStatChange& Change) { return Change.bAffectsSelf && Change.Stages != 0; })
		&& FMath::RandRange(1, 100) <= SkillCDO->StatStageChance)
	{
		for (const FGF_SkillStatChange& Change : SkillCDO->StatChanges)
		{
			if (!Change.bAffectsSelf || Change.Stages == 0)
			{
				continue;
			}

			FGF_StatStageOutcome Planned;
			Planned.Stat = Change.ToStatStage();
			Planned.Requested = Change.Stages;
			Planned.bSelfInflicted = true;
			Resolution.SelfStatStages.Add(Planned);
		}
	}

	if (bSelfEffectsLand && SkillCDO->HealAmount > 0)
	{
		// Dokimon heal: ceil((0.012 * Level * Heal + 2) * Rand * STAB). The user's
		// stats play no part, only its level and whether the skill shares its element.
		const float STAB = UGF_CreatureRulesSettings::GetSTABMultiplier(
			SkillCDO->Type, Attacker->PrimaryElement, Attacker->SecondaryElement);

		Resolution.SelfHeal += FMath::CeilToFloat(
			(0.012f * Attacker->CurrentStats.Level * SkillCDO->HealAmount + 2.0f) * FMath::FRandRange(0.95f, 1.25f) * STAB);
	}

	Resolution.bSelfStatsCleansed = bSelfEffectsLand && SkillCDO->bCleansesStatChanges;
	Resolution.bWillRecharge = SkillCDO->bRequiresRecharge && bAnythingConnected;

	return Resolution;
}

//======================================================================================
// APPLY
//======================================================================================

bool UGF_BattleResolver::ApplyResolution(FGF_ActionResolution& Resolution)
{
	if (Resolution.bApplied)
	{
		// Applying twice would subtract the same damage again. The usual cause is
		// a presentation retry, which must not cost the target its HP a second time.
		UE_LOG(LogGF_BattleResolve, Warning,
			TEXT("ApplyResolution called twice for %s. Ignored."), *Resolution.Actor.ToString());
		return false;
	}

	Resolution.bApplied = true;

	AGF_Creature* Attacker = Resolution.ActorCreature;

	// The recharge turn is the whole action: no Uses spent, no Protect streak
	// touched, nothing else applied. Clearing the flag here, not at planning,
	// keeps a plan that is never committed from cancelling the recharge.
	if (Resolution.bRechargeTurn)
	{
		if (Attacker != nullptr)
		{
			Attacker->bMustRecharge = false;
			Attacker->bHasActedSinceEntering = true;
			Attacker->MarkAsMovedThisTurn();
			Resolution.ActorHPAfter = Attacker->CurrentStats.CurrentHP;
			Resolution.bActorDowned = Attacker->IsDowned();
		}
		return true;
	}

	if (Resolution.ActionType == EGF_BattleActionType::Brace)
	{
		ApplyBrace(Attacker);
	}

	// Spent even on a miss -- the Use goes when the skill is thrown, not when it
	// connects. Only Last Resort (INDEX_NONE) is free.
	if (Resolution.ActionType == EGF_BattleActionType::Skill && !Resolution.bFailed)
	{
		SpendSkillUses(Attacker, Resolution.SkillIndexToSpend);
	}

	if (Attacker != nullptr)
	{
		Attacker->MarkAsMovedThisTurn();
		Attacker->bHasActedSinceEntering = true;
	}

	const AGF_SkillDefinition* SkillCDO =
		Resolution.SkillClass ? Resolution.SkillClass.GetDefaultObject() : nullptr;

	// A non-Protect skill breaks the consecutive-Protect streak, which is what
	// stops Protect being spammable. Done before this skill's own Protect runs.
	if (Attacker != nullptr && Resolution.ActionType == EGF_BattleActionType::Skill && SkillCDO != nullptr)
	{
		if (SkillCDO->bIsProtectSkill)
		{
			Resolution.bProtectSucceeded = UGF_BattleComponent::ApplyProtect(Attacker);
		}
		else
		{
			UGF_BattleComponent::ResetProtectStreak(Attacker);
		}

		if (SkillCDO->bIsBideSkill)
		{
			UGF_BattleComponent::StartBide(Attacker, SkillCDO->BideStoreTurns);
		}
	}

	for (FGF_TargetResolution& TargetResult : Resolution.Targets)
	{
		AGF_Creature* Defender = TargetResult.Creature;
		if (Defender == nullptr)
		{
			continue;
		}

		if (TargetResult.DamageToApply > 0.0f)
		{
			ApplyHPDelta(Defender, -TargetResult.DamageToApply, TargetResult.HPAfter, TargetResult.bDowned);
			Defender->bTookDamageThisTurn = true;

			// Brace stores what it takes, so a biding creature releasing next round
			// pays back everything that got through.
			if (UGF_BattleComponent::IsBiding(Defender))
			{
				UGF_BattleComponent::AddBideDamage(Defender, TargetResult.DamageToApply);
			}
		}

		// Everything below only happens to a creature still standing. Statusing a
		// corpse produces messages about a creature the player just watched fall.
		if (!TargetResult.bConnected || Defender->IsDowned())
		{
			continue;
		}

		// --- status ---------------------------------------------------------
		if (TargetResult.Status.bRolled && !TargetResult.Status.bBlocked)
		{
			const bool bLanded = Defender->ApplyStatusCondition(TargetResult.Status.Status);
			TargetResult.Status.bApplied = bLanded;

			if (bLanded)
			{
				TargetResult.Status.Message = FText::Format(
					LOCTEXT("StatusApplied", "{0} was afflicted: {1}!"),
					TargetResult.DisplayName,
					UEnum::GetDisplayValueAsText(TargetResult.Status.Status));
			}
			else
			{
				// ApplyStatusCondition refuses to stack a second condition. That is
				// the usual reason, and it deserves a line rather than silence.
				TargetResult.Status.Message = FText::Format(
					LOCTEXT("StatusAlready", "{0} is already afflicted."), TargetResult.DisplayName);
			}
		}

		// --- stat stages ----------------------------------------------------
		for (FGF_StatStageOutcome& Stage : TargetResult.StatStages)
		{
			Stage = ApplyStatStage(Defender, Stage.Stat, Stage.Requested, /*bSelfInflicted*/ false);
		}

		// --- flinch ---------------------------------------------------------
		if (TargetResult.bFlinched)
		{
			FString TraitMessage;
			TargetResult.bFlinched = Defender->ApplyFlinch(TraitMessage);

			if (TargetResult.bFlinched)
			{
				TargetResult.FlinchMessage = FText::FromString(Defender->GetFlinchMessage());
			}

			// Resolute raises Speed whenever its holder flinches, and says so.
			if (!TraitMessage.IsEmpty())
			{
				Resolution.ContactMessages.Add(FText::FromString(TraitMessage));
			}
		}

		// --- trapping -------------------------------------------------------
		if (TargetResult.bTrapped && SkillCDO != nullptr)
		{
			TargetResult.bTrapped = UGF_BattleComponent::ApplyPartialTrap(
				Defender, SkillCDO->Name, SkillCDO->TrapMinTurns, SkillCDO->TrapMaxTurns);
		}

		// --- contact traits -------------------------------------------------
		// Bramblehide, Livewire and the rest fire off the TARGET onto the
		// ATTACKER, so they are collected here and land on the user below.
		if (Attacker != nullptr && SkillCDO != nullptr && SkillCDO->bMakesContact
			&& TargetResult.DamageToApply > 0.0f)
		{
			const FGF_TraitContactResult Contact =
				UGF_CreatureTraitLibrary::OnContactMade(Attacker, Defender, Resolution.SkillClass);

			if (Contact.bTriggered)
			{
				Resolution.SelfDamage += Contact.DamageToAttacker;

				if (Contact.StatusToApply != EGF_STATUSEffect::None)
				{
					Resolution.ContactStatusOnSelf.Status = Contact.StatusToApply;
					Resolution.ContactStatusOnSelf.bRolled = true;
					Resolution.ContactStatusOnSelf.bApplied =
						Attacker->ApplyStatusCondition(Contact.StatusToApply);
				}

				if (!Contact.Message.IsEmpty())
				{
					Resolution.ContactMessages.Add(FText::FromString(Contact.Message));
				}
			}
		}
	}

	// --- effects on the user ------------------------------------------------
	if (Attacker != nullptr)
	{
		// Before this skill's own stat changes, so a skill that cleanses and then
		// buffs ends up with the buff rather than wiping it.
		if (Resolution.bSelfStatsCleansed)
		{
			if (UGF_CreatureStatStageComponent* Stages = Attacker->FindComponentByClass<UGF_CreatureStatStageComponent>())
			{
				Stages->ResetAllStatStages();
			}
		}

		if (Resolution.bWillRecharge)
		{
			Attacker->bMustRecharge = true;
		}

		for (FGF_StatStageOutcome& Stage : Resolution.SelfStatStages)
		{
			Stage = ApplyStatStage(Attacker, Stage.Stat, Stage.Requested, /*bSelfInflicted*/ true);
		}

		if (Resolution.bSelfStatusCleared)
		{
			Attacker->ClearStatusCondition();
		}

		if (Resolution.bCritStageRaised && SkillCDO != nullptr)
		{
			Attacker->CritStageBoost += SkillCDO->CritStageBoostAmount;
		}

		// Rising Slash ramps on a connected hit and resets on anything else.
		bool bAnyConnected = false;
		for (const FGF_TargetResolution& TargetResult : Resolution.Targets)
		{
			bAnyConnected |= TargetResult.bConnected;
		}

		if (SkillCDO != nullptr && SkillCDO->bIsFuryCutterStyle && bAnyConnected)
		{
			UGF_BattleComponent::IncrementFuryCutter(Attacker);
		}
		else if (Resolution.ActionType == EGF_BattleActionType::Skill)
		{
			UGF_BattleComponent::ResetFuryCutterStreak(Attacker);
		}
	}

	if (Attacker != nullptr && (Resolution.SelfDamage > 0.0f || Resolution.SelfHeal > 0.0f))
	{
		ApplyHPDelta(Attacker, Resolution.SelfHeal - Resolution.SelfDamage,
			Resolution.ActorHPAfter, Resolution.bActorDowned);
	}
	else if (Attacker != nullptr)
	{
		Resolution.ActorHPAfter = Attacker->CurrentStats.CurrentHP;
		Resolution.bActorDowned = Attacker->IsDowned();
	}

	return true;
}

FGF_ActionResolution UGF_BattleResolver::ResolveActionImmediately(
	UGF_BattleComponent* DamageComponent,
	const UGF_BattleBoard* Board,
	const FGF_BattleAction& Action,
	EGF_WeatherType Weather)
{
	FGF_ActionResolution Resolution = PlanAction(DamageComponent, Board, Action, Weather);
	ApplyResolution(Resolution);
	return Resolution;
}

//======================================================================================
// PIECES
//======================================================================================

FGF_StatStageOutcome UGF_BattleResolver::ApplyStatStage(
	AGF_Creature* Creature, EGF_StatStages Stat, int32 Amount, bool bSelfInflicted)
{
	FGF_StatStageOutcome Outcome;
	Outcome.Stat = Stat;
	Outcome.Requested = Amount;
	Outcome.bSelfInflicted = bSelfInflicted;

	if (Creature == nullptr || Stat == EGF_StatStages::None || Amount == 0)
	{
		return Outcome;
	}

	// The asset names a direction (AttackUp / AttackDown) AND carries a signed
	// amount, so the two can disagree. The amount wins, because it is what the
	// stage component actually applies -- but a mismatch is almost always an
	// author leaving the default -1 on an Up skill, so say so out loud.
	const bool bEnumSaysUp =
		Stat == EGF_StatStages::AttackUp   || Stat == EGF_StatStages::DefenseUp ||
		Stat == EGF_StatStages::MagicUp    || Stat == EGF_StatStages::PoiseUp   ||
		Stat == EGF_StatStages::SpeedUp    || Stat == EGF_StatStages::AccuracyUp ||
		Stat == EGF_StatStages::EvasionUp;

	if (bEnumSaysUp != (Amount > 0))
	{
		UE_LOG(LogGF_BattleResolve, Warning,
			TEXT("Stat stage mismatch on %s: the enum says %s but the amount is %d. Using the amount."),
			*Creature->Name.ToString(), bEnumSaysUp ? TEXT("up") : TEXT("down"), Amount);
	}

	UGF_CreatureStatStageComponent* Stages = Creature->FindComponentByClass<UGF_CreatureStatStageComponent>();
	if (Stages == nullptr)
	{
		// Silence here cost a long debugging session: every stat move appeared to
		// resolve, reported no error, and changed nothing. The component is what
		// stores the stages, and a creature Blueprint has to carry one.
		UE_LOG(LogTemp, Warning,
			TEXT("%s has no GF_CreatureStatStageComponent, so its %s cannot change. ")
			TEXT("Add the component to the creature Blueprint."),
			*Creature->Name.ToString(),
			*UEnum::GetDisplayValueAsText(Stat).ToString());
		return Outcome;
	}

	Outcome.ActualChange = Stages->ApplyStatStageChange(Stat, Amount, bSelfInflicted);
	Outcome.bBlockedByTrait = Stages->bBlockedByTrait;

	// Zero change with no trait block means the stat was already at +6 or -6.
	// That is a different message from "a trait stopped you".
	Outcome.bAtLimit = (Outcome.ActualChange == 0) && !Outcome.bBlockedByTrait;

	// Presentation hook. Every stat change in the game passes through here --
	// skills, items, traits, end-of-turn effects -- so this is the one place a
	// rise/fall effect needs to be raised from. Only fired when something
	// actually moved, so a blocked or already-capped change plays nothing.
	if (Outcome.ActualChange != 0)
	{
		Creature->NotifyStatStageChanged(Stat, Outcome.ActualChange, Outcome.bAtLimit);
	}

	if (Outcome.bBlockedByTrait)
	{
		Outcome.Message = FText::FromString(Stages->LastTraitBlockMessage);
	}
	else
	{
		// Qualified for the same reason as the round-end lines: "Attack fell!"
		// against two identically named creatures says nothing about whose.
		const FText Qualified = QualifiedName(Creature, FText::FromName(Creature->Name));

		Outcome.Message = FText::FromString(Stages->GetStatStageMessage(
			Stat, Outcome.ActualChange, bSelfInflicted, Qualified.ToString()));
	}

	return Outcome;
}

EGF_StatStages UGF_BattleResolver::PickRandomStatStage(bool bRaise)
{
	// Accuracy and evasion are left out on purpose. A random roll landing on
	// accuracy reads as nothing happening, because there is no number on screen
	// for the player to watch move.
	static const EGF_StatStages Ups[] = {
		EGF_StatStages::AttackUp, EGF_StatStages::DefenseUp, EGF_StatStages::MagicUp,
		EGF_StatStages::PoiseUp, EGF_StatStages::SpeedUp };

	static const EGF_StatStages Downs[] = {
		EGF_StatStages::AttackDown, EGF_StatStages::DefenseDown, EGF_StatStages::MagicDown,
		EGF_StatStages::PoiseDown, EGF_StatStages::SpeedDown };

	const int32 Index = FMath::RandRange(0, 4);
	return bRaise ? Ups[Index] : Downs[Index];
}

bool UGF_BattleResolver::ApplyBrace(AGF_Creature* Creature)
{
	if (Creature == nullptr || Creature->IsDowned())
	{
		return false;
	}

	Creature->bIsBracing = true;

	if (UGF_CreatureStatStageComponent* Stages = Creature->FindComponentByClass<UGF_CreatureStatStageComponent>())
	{
		Stages->ApplyStatStageChange(EGF_StatStages::PoiseUp, 1, /*bSelfInflicted*/ true);
	}

	return true;
}

void UGF_BattleResolver::SpendSkillUses(AGF_Creature* Creature, int32 SkillIndex)
{
	if (Creature == nullptr || !Creature->SkillUses.IsValidIndex(SkillIndex))
	{
		return;
	}

	FGF_SkillUses& Uses = Creature->SkillUses[SkillIndex];
	Uses.CurrentUses = FMath::Max(0, Uses.CurrentUses - 1);
}

bool UGF_BattleResolver::TickSleepAtTurnStart(AGF_Creature* Creature, bool& bWokeUp)
{
	bWokeUp = false;

	if (Creature == nullptr || Creature->Status != EGF_STATUS::Sleeping)
	{
		return false;
	}

	// Dokimon sleep: a flat chance to wake each turn, and the counter is only the
	// cap on how many turns can be lost before it wakes regardless.
	const float WakeChance = GetDefault<UGF_CreatureRulesSettings>()->SleepWakeChancePercent;
	if (Creature->SleepCounter <= 0 || FMath::FRandRange(0.0f, 100.0f) < WakeChance)
	{
		Creature->ClearStatusCondition();
		bWokeUp = true;
		// Woke up on its own turn, so it still gets to act.
		return false;
	}

	--Creature->SleepCounter;
	return true;
}

//======================================================================================
// END OF ROUND
//======================================================================================

TArray<FGF_RoundEndTick> UGF_BattleResolver::ResolveRoundEnd(
	UObject* WorldContext,
	const UGF_BattleBoard* Board,
	EGF_WeatherType Weather,
	const FGF_ChipDamageConfig& Config)
{
	TArray<FGF_RoundEndTick> Ticks;

	if (Board == nullptr)
	{
		return Ticks;
	}

	// Player line then enemy line, seat order within each. Fixed so the same
	// board always ticks the same way and a message sequence is reproducible.
	for (const FGF_BattleSlot& Slot : Board->GetAllLivingSlots())
	{
		FGF_RoundEndTick Tick = ResolveRoundEndForSlot(WorldContext, Board, Slot, Weather, Config);
		if (Tick.bAnythingHappened)
		{
			Ticks.Add(Tick);
		}
	}

	return Ticks;
}

FGF_RoundEndTick UGF_BattleResolver::ResolveRoundEndForSlot(
	UObject* WorldContext,
	const UGF_BattleBoard* Board,
	const FGF_BattleSlot& Slot,
	EGF_WeatherType Weather,
	const FGF_ChipDamageConfig& Config)
{
	FGF_RoundEndTick Tick;

	if (Board == nullptr)
	{
		return Tick;
	}

	{
		AGF_Creature* Creature = Board->GetCreatureInSlot(Slot);
		if (Creature == nullptr || Creature->IsDowned())
		{
			return Tick;
		}

		Tick.Slot = Slot;
		Tick.Creature = Creature;
		Tick.DisplayName = Board->GetDisplayNameForSlot(Slot);
		Tick.HPBefore = Creature->CurrentStats.CurrentHP;
		Tick.HPAfter = Tick.HPBefore;

		const FText Name = QualifiedName(Creature, Tick.DisplayName);
		const float MaxHP = Creature->CurrentStats.MaxHP;

		// --- status ---------------------------------------------------------
		if (Creature->Status == EGF_STATUS::Burned && Config.BurnFraction > 0.0f)
		{
			Tick.StatusDamage = FMath::Max(1.0f, FMath::RoundToFloat(MaxHP * Config.BurnFraction));
			Tick.Messages.Add(FText::Format(LOCTEXT("BurnTick", "The burn eats into {0}."), Name));
		}
		else if (Creature->Status == EGF_STATUS::Poisoned && Config.PoisonFraction > 0.0f)
		{
			Tick.StatusDamage = FMath::Max(1.0f, FMath::RoundToFloat(MaxHP * Config.PoisonFraction));
			Tick.Messages.Add(FText::Format(LOCTEXT("PoisonTick", "The venom works through {0}."), Name));
		}

		// --- weather --------------------------------------------------------
		// Stillair on either creature suppresses all weather, so ask the trait
		// layer what the weather effectively is rather than trusting the raw value.
		const EGF_WeatherType EffectiveWeather =
			UGF_CreatureTraitLibrary::GetEffectiveWeather(Weather, Creature, nullptr);

		if (Config.WeatherFraction > 0.0f
			&& !IsImmuneToWeatherChip(Creature->PrimaryElement, EffectiveWeather)
			&& !IsImmuneToWeatherChip(Creature->SecondaryElement, EffectiveWeather))
		{
			Tick.WeatherDamage = FMath::Max(1.0f, MaxHP * Config.WeatherFraction);
			Tick.Messages.Add(FText::Format(
				EffectiveWeather == EGF_WeatherType::Sandstorm
					? LOCTEXT("SandTick", "The sand flays {0}.")
					: LOCTEXT("HailTick", "The hail batters {0}."),
				Name));
		}

		// --- partial trap ---------------------------------------------------
		if (UGF_BattleComponent::IsPartiallyTrapped(Creature))
		{
			Tick.TrapDamage = static_cast<float>(UGF_BattleComponent::GetPartialTrapDamage(Creature));

			const FText TrapName = FText::FromName(Creature->PartialTrapMoveName);
			Tick.Messages.Add(FText::Format(LOCTEXT("TrapTick", "{1} tightens on {0}."), Name, TrapName));

			bool bJustEnded = false;
			UGF_BattleComponent::TickPartialTrap(Creature, bJustEnded);
			Tick.bTrapEnded = bJustEnded;

			if (bJustEnded)
			{
				Tick.Messages.Add(FText::Format(LOCTEXT("TrapEnd", "{0} pulls free of {1}."), Name, TrapName));
			}
		}

		// --- apply the chip -------------------------------------------------
		const float TotalChip = Tick.StatusDamage + Tick.WeatherDamage + Tick.TrapDamage;
		if (TotalChip > 0.0f)
		{
			ApplyHPDelta(Creature, -TotalChip, Tick.HPAfter, Tick.bDowned);
		}

		// --- traits and held items -----------------------------------------
		// After the chip, so a Sustain Charm can save a creature the chip would
		// otherwise have downed -- and so a trait heal is not wasted on HP that
		// is about to be subtracted again.
		if (!Creature->IsDowned())
		{
			const FGF_TraitEndOfTurnResult TraitResult =
				UGF_CreatureTraitLibrary::OnEndOfTurn(Creature, EffectiveWeather);

			if (TraitResult.bTriggered)
			{
				Tick.TraitHeal = TraitResult.HealAmount;
				Tick.bStatusCured = TraitResult.bStatusCured;

				if (!TraitResult.Message.IsEmpty())
				{
					Tick.Messages.Add(FText::FromString(TraitResult.Message));
				}
			}

			UGF_HeldItemBattleHelper::HandleEndOfTurnEffects(WorldContext, Creature);

			Tick.HPAfter = Creature->CurrentStats.CurrentHP;
			Tick.bDowned = Creature->IsDowned();
		}

		Tick.bAnythingHappened = TotalChip > 0.0f || Tick.TraitHeal > 0.0f
			|| Tick.bStatusCured || Tick.bTrapEnded || Tick.Messages.Num() > 0;
	}

	return Tick;
}

FText UGF_BattleResolver::BuildAnnounceMessage(const FGF_ActionResolution& Plan)
{
	const FText Name = QualifiedName(Plan.ActorCreature, Plan.ActorDisplayName);

	// The creature never got as far as choosing: the recharge or flinch line is
	// the whole turn.
	if (Plan.bRechargeTurn || Plan.bFlinchTurn)
	{
		return Plan.FailMessage;
	}

	switch (Plan.ActionType)
	{
	case EGF_BattleActionType::Skill:
	{
		const AGF_SkillDefinition* SkillCDO = Plan.SkillClass ? Plan.SkillClass.GetDefaultObject() : nullptr;
		if (SkillCDO == nullptr)
		{
			return FText::GetEmpty();
		}

		return FText::Format(LOCTEXT("AnnounceSkill", "{0} used {1}!"), Name, FText::FromName(SkillCDO->Name));
	}

	case EGF_BattleActionType::Brace:
		return FText::Format(LOCTEXT("AnnounceBrace", "{0} braces for impact."), Name);

	case EGF_BattleActionType::Flee:
		// The attempt only. Whether it worked is a result, and BuildActionMessages
		// reports it off the same roll.
		return FText::Format(LOCTEXT("AnnounceFlee", "{0} tries to run!"), Name);

	// The plan carries neither the item, the Core nor the arriving creature, so
	// these cannot name them.
	case EGF_BattleActionType::Item:
		return FText::Format(LOCTEXT("AnnounceItem", "{0} uses an item."), Name);

	case EGF_BattleActionType::Marque:
		return FText::Format(LOCTEXT("AnnounceMarque", "{0} attempts a claim."), Name);

	case EGF_BattleActionType::Swap:
		return FText::Format(LOCTEXT("AnnounceSwap", "{0} falls back."), Name);

	case EGF_BattleActionType::Pass:
		// Locked in, incapacitated, or no legal order. Silence reads as a
		// dropped turn, which is worse than an unexplained one.
		return FText::Format(LOCTEXT("AnnouncePass", "{0} cannot move."), Name);

	default:
		return FText::GetEmpty();
	}
}

TArray<FText> UGF_BattleResolver::BuildActionMessages(const FGF_ActionResolution& Plan)
{
	TArray<FText> Messages;

	// The recharge or flinch line was the announcement. Every other failure is
	// a result.
	if (Plan.bRechargeTurn || Plan.bFlinchTurn)
	{
		return Messages;
	}

	// Rolled when the turn began and carried on the plan, so this line and the
	// battle ending come from the same roll.
	if (Plan.ActionType == EGF_BattleActionType::Flee)
	{
		if (Plan.bFleeEscaped)
		{
			Messages.Add(LOCTEXT("ActionFleeEscaped", "Got away safely!"));
		}
		else
		{
			Messages.Add(Plan.FleeBlocker != nullptr
				? FText::Format(LOCTEXT("ActionFleeBlocked", "{0} cuts off the escape!"),
					QualifiedName(Plan.FleeBlocker, Plan.FleeBlockerDisplayName))
				: LOCTEXT("ActionFleeFailed", "Couldn't get away!"));
		}
		return Messages;
	}

	if (Plan.bFailed)
	{
		if (!Plan.FailMessage.IsEmpty())
		{
			Messages.Add(Plan.FailMessage);
		}
		return Messages;
	}

	// Brace, Item, Swap and Pass have said all they have to say by now.
	if (Plan.ActionType != EGF_BattleActionType::Skill)
	{
		return Messages;
	}

	const AGF_SkillDefinition* SkillCDO = Plan.SkillClass ? Plan.SkillClass.GetDefaultObject() : nullptr;
	const FText ActorName = QualifiedName(Plan.ActorCreature, Plan.ActorDisplayName);

	// With one target "It's super effective!" is unambiguous. With several it
	// has to say whose, or the lines cannot be told apart.
	const bool bNameTargets = Plan.Targets.Num() > 1;

	//----------------------------------------------------------------------------
	// Each target
	//----------------------------------------------------------------------------

	for (const FGF_TargetResolution& Target : Plan.Targets)
	{
		const FText TargetName = QualifiedName(Target.Creature, Target.DisplayName);

		if (Target.bBlockedByProtect)
		{
			Messages.Add(FText::Format(LOCTEXT("ActionProtected", "{0} protected itself!"), TargetName));
			continue;
		}

		if (Target.bSemiInvulnerable)
		{
			Messages.Add(FText::Format(LOCTEXT("ActionOutOfReach", "{0} is out of reach!"), TargetName));
			continue;
		}

		if (Target.DamageToApply > 0.0f)
		{
			if (Target.HitCount > 1)
			{
				Messages.Add(FText::Format(LOCTEXT("ActionMultiHit", "Hit {0} times!"), FText::AsNumber(Target.HitCount)));
			}

			if (Target.Damage.bWasCritical)
			{
				Messages.Add(bNameTargets
					? FText::Format(LOCTEXT("ActionCritNamed", "A critical hit on {0}!"), TargetName)
					: LOCTEXT("ActionCrit", "A critical hit!"));
			}

			if (Target.Damage.bSuperEffective)
			{
				Messages.Add(bNameTargets
					? FText::Format(LOCTEXT("ActionSuperNamed", "It's super effective on {0}!"), TargetName)
					: LOCTEXT("ActionSuper", "It's super effective!"));
			}
			else if (Target.Damage.bNotEffective)
			{
				Messages.Add(bNameTargets
					? FText::Format(LOCTEXT("ActionResistedNamed", "It's not very effective on {0}..."), TargetName)
					: LOCTEXT("ActionResisted", "It's not very effective..."));
			}

			if (Target.bReducedByBrace)
			{
				Messages.Add(FText::Format(LOCTEXT("ActionBraced", "{0} braced against the blow."), TargetName));
			}
		}
		else if (Target.Damage.bImmune)
		{
			// The formula's own line when it has one: a trait that blocked the
			// skill names itself there, and that is worth more than a generic.
			Messages.Add(!Target.Damage.DamageMessage.IsEmpty()
				? FText::FromString(Target.Damage.DamageMessage)
				: FText::Format(LOCTEXT("ActionImmune", "It doesn't affect {0}..."), TargetName));
			continue;
		}
		else if (Target.Damage.bMissed)
		{
			Messages.Add(FText::Format(LOCTEXT("ActionMissed", "{0} avoided the attack!"), TargetName));
			continue;
		}

		// Nothing below was carried out on a creature that fell to the hit.
		if (Target.bDowned)
		{
			Messages.Add(FText::Format(LOCTEXT("ActionDowned", "{0} went down!"), TargetName));
			continue;
		}

		if (!Target.bConnected)
		{
			continue;
		}

		if (Target.Status.bBlocked && !Target.Status.BlockMessage.IsEmpty())
		{
			Messages.Add(Target.Status.BlockMessage);
		}
		else if (!Target.Status.Message.IsEmpty())
		{
			Messages.Add(Target.Status.Message);
		}

		for (const FGF_StatStageOutcome& Stage : Target.StatStages)
		{
			if (!Stage.Message.IsEmpty())
			{
				Messages.Add(Stage.Message);
			}
		}

		if (Target.bTrapped && SkillCDO != nullptr)
		{
			Messages.Add(FText::Format(LOCTEXT("ActionTrapped", "{0} is caught in {1}!"),
				TargetName, FText::FromName(SkillCDO->Name)));
		}

		// No line for bFlinched here. "flinched and couldn't move!" belongs to
		// the flinched creature's own turn, where PlanAction fails it with that
		// message -- printing it on the hit as well said it before it was true.
	}

	//----------------------------------------------------------------------------
	// The user
	//----------------------------------------------------------------------------

	if (SkillCDO != nullptr && SkillCDO->bIsProtectSkill)
	{
		Messages.Add(Plan.bProtectSucceeded
			? FText::Format(LOCTEXT("ActionProtectUp", "{0} guards itself."), ActorName)
			: LOCTEXT("ActionProtectFailed", "But it failed!"));
	}

	if (Plan.bSelfStatsCleansed)
	{
		Messages.Add(FText::Format(LOCTEXT("ActionCleansed", "{0}'s stat changes were wiped away."), ActorName));
	}

	for (const FGF_StatStageOutcome& Stage : Plan.SelfStatStages)
	{
		if (!Stage.Message.IsEmpty())
		{
			Messages.Add(Stage.Message);
		}
	}

	if (Plan.bSelfStatusCleared)
	{
		Messages.Add(FText::Format(LOCTEXT("ActionRefreshed", "{0} shook off its condition."), ActorName));
	}

	if (Plan.bCritStageRaised)
	{
		Messages.Add(FText::Format(LOCTEXT("ActionFocused", "{0} is sharpening its focus."), ActorName));
	}

	Messages.Append(Plan.ContactMessages);

	if (Plan.ContactStatusOnSelf.bApplied)
	{
		Messages.Add(FText::Format(LOCTEXT("StatusApplied", "{0} was afflicted: {1}!"),
			ActorName, UEnum::GetDisplayValueAsText(Plan.ContactStatusOnSelf.Status)));
	}

	if (Plan.bDrainBackfired)
	{
		Messages.Add(FText::Format(LOCTEXT("ActionDrainBackfired", "The foul sap burns {0}!"), ActorName));
	}
	else if (Plan.SelfHeal > 0.0f)
	{
		Messages.Add(FText::Format(
			SkillCDO != nullptr && SkillCDO->bIsDrainingSkill
				? LOCTEXT("ActionDrained", "{0} drew strength from the hit.")
				: LOCTEXT("ActionHealed", "{0} recovered health."),
			ActorName));
	}

	if (SkillCDO != nullptr && SkillCDO->bHasRecoil && Plan.SelfDamage > 0.0f)
	{
		Messages.Add(FText::Format(LOCTEXT("ActionRecoil", "{0} is hurt by the recoil!"), ActorName));
	}

	// Recoil, a contact trait or a self-KO skill. Skipped when the user was also
	// one of its own targets and has already been reported there.
	if (Plan.bActorDowned && !Plan.Targets.ContainsByPredicate(
			[&Plan](const FGF_TargetResolution& Target) { return Target.bDowned && Target.Creature == Plan.ActorCreature; }))
	{
		Messages.Add(FText::Format(LOCTEXT("ActionDowned", "{0} went down!"), ActorName));
	}

	return Messages;
}

TArray<FText> UGF_BattleResolver::BuildRoundEndMessages(const TArray<FGF_RoundEndTick>& Ticks)
{
	TArray<FText> Messages;

	for (const FGF_RoundEndTick& Tick : Ticks)
	{
		Messages.Append(Tick.Messages);

		// Not carried by the tick itself, and the one line the player most needs
		// when chip damage is what finished a creature off.
		if (Tick.bDowned)
		{
			Messages.Add(FText::Format(LOCTEXT("ActionDowned", "{0} went down!"),
				QualifiedName(Tick.Creature, Tick.DisplayName)));
		}
	}

	return Messages;
}

#undef LOCTEXT_NAMESPACE
