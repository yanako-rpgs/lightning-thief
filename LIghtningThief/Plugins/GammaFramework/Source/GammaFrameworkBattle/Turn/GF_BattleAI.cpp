#include "GF_BattleAI.h"

#include "GF_BattleBoard.h"
#include "GF_BattleManagementFunctions.h"
#include "GF_Creature.h"
#include "GF_CreatureStatStageComponent.h"
#include "GF_SkillDefinition.h"
#include "GF_TurnOrderLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogGF_BattleAI, Log, All);

namespace
{
	/** Stage multiplier for one stat, or 1.0 when the creature has no stage component. */
	float StageMult(const AGF_Creature* Creature, EGF_StatStages Stat)
	{
		if (Creature == nullptr)
		{
			return 1.0f;
		}
		if (const UGF_CreatureStatStageComponent* Stages = Creature->FindComponentByClass<UGF_CreatureStatStageComponent>())
		{
			return Stages->GetStatMultiplier(Stat);
		}
		return 1.0f;
	}
}

//======================================================================================
// ESTIMATION
//======================================================================================

float UGF_BattleAILibrary::EstimateDamage(
	const AGF_Creature* Attacker,
	const AGF_Creature* Defender,
	TSubclassOf<AGF_SkillDefinition> SkillClass,
	EGF_WeatherType Weather)
{
	if (Attacker == nullptr || Defender == nullptr || !SkillClass)
	{
		return 0.0f;
	}

	const AGF_SkillDefinition* Skill = SkillClass.GetDefaultObject();
	if (Skill == nullptr || Skill->Split == EGF_SkillCategory::Status || Skill->Power <= 0.0f)
	{
		return 0.0f;
	}

	const float TypeEff = BattleManagementFunctions::GetTypeEffectiveness(
		Skill->Type, Defender->PrimaryElement, Defender->SecondaryElement);

	if (TypeEff <= 0.0f)
	{
		return 0.0f;
	}

	const float STAB = BattleManagementFunctions::GetSTABBonus(
		Skill->Type, Attacker->PrimaryElement, Attacker->SecondaryElement);

	const bool bPhysical = (Skill->Split == EGF_SkillCategory::Physical);

	const float Atk = (bPhysical ? Attacker->CurrentStats.Attack : Attacker->CurrentStats.Magic)
		* StageMult(Attacker, bPhysical ? EGF_StatStages::AttackUp : EGF_StatStages::MagicUp);

	const float Def = FMath::Max(1.0f,
		(bPhysical ? Defender->CurrentStats.Defense : Defender->CurrentStats.Poise)
		* StageMult(Defender, bPhysical ? EGF_StatStages::DefenseUp : EGF_StatStages::PoiseUp));

	// The classic damage shape, so the number comes out in HP rather than in
	// arbitrary score units. That matters: the finishing-blow check compares it
	// straight against the target's remaining HP.
	const float Level = static_cast<float>(FMath::Max(1, Attacker->CurrentStats.Level));
	float Damage = (((2.0f * Level / 5.0f) + 2.0f) * Skill->Power * (Atk / Def)) / 50.0f + 2.0f;

	Damage *= STAB * TypeEff;

	// Accuracy is folded in as expected value rather than modelled as a roll --
	// a 70%-accurate skill that hits twice as hard should still rank first.
	if (Skill->Accuracy > 0.0f && Skill->Accuracy < 100.0f)
	{
		Damage *= (Skill->Accuracy / 100.0f);
	}

	if (Attacker->Status == EGF_STATUS::Burned && bPhysical)
	{
		Damage *= 0.5f;
	}

	return FMath::Max(0.0f, Damage);
}

TArray<int32> UGF_BattleAILibrary::GetUsableSkillIndices(const AGF_Creature* Creature)
{
	TArray<int32> Result;
	if (Creature == nullptr)
	{
		return Result;
	}

	for (int32 i = 0; i < Creature->Skills.Num(); ++i)
	{
		if (!Creature->Skills[i])
		{
			continue;
		}

		// A creature with no Uses array authored is treated as having Uses. That
		// is the common state for a freshly spawned wild creature and refusing to
		// let it attack would be worse than assuming it can.
		const bool bHasUses = !Creature->SkillUses.IsValidIndex(i) || Creature->SkillUses[i].CurrentUses > 0;
		if (bHasUses)
		{
			Result.Add(i);
		}
	}

	return Result;
}

//======================================================================================
// SCORING
//======================================================================================

TArray<FGF_AIScoredOption> UGF_BattleAILibrary::ScoreAllOptions(
	const UGF_BattleBoard* Board,
	const FGF_BattleSlot& Slot,
	const FGF_BattleAIProfile& Profile,
	EGF_WeatherType Weather)
{
	TArray<FGF_AIScoredOption> Options;

	if (Board == nullptr)
	{
		return Options;
	}

	const AGF_Creature* Attacker = Board->GetCreatureInSlot(Slot);
	const TArray<int32> Usable = GetUsableSkillIndices(Attacker);
	const TArray<FGF_BattleSlot> Enemies = Board->GetLivingOpponentsOf(Slot);

	if (Attacker == nullptr || Usable.Num() == 0 || Enemies.Num() == 0)
	{
		return Options;
	}

	for (int32 SkillIndex : Usable)
	{
		const TSubclassOf<AGF_SkillDefinition> SkillClass = Attacker->Skills[SkillIndex];
		const AGF_SkillDefinition* Skill = SkillClass.GetDefaultObject();
		if (Skill == nullptr)
		{
			continue;
		}

		const bool bSpread = (Skill->TargetShape == EGF_SkillTargetShape::AllEnemies
			|| Skill->TargetShape == EGF_SkillTargetShape::Everyone);

		for (const FGF_BattleSlot& EnemySlot : Enemies)
		{
			const AGF_Creature* Defender = Board->GetCreatureInSlot(EnemySlot);
			if (Defender == nullptr)
			{
				continue;
			}

			FGF_AIScoredOption Option;
			Option.SkillIndex = SkillIndex;
			Option.SkillClass = SkillClass;
			Option.Target = EnemySlot;
			Option.TypeEffectiveness = BattleManagementFunctions::GetTypeEffectiveness(
				Skill->Type, Defender->PrimaryElement, Defender->SecondaryElement);

			Option.EstimatedDamage = EstimateDamage(Attacker, Defender, SkillClass, Weather);

			if (Skill->Split == EGF_SkillCategory::Status)
			{
				// A status skill is worth something only if it lands something new.
				// Doubling up a status is a wasted action, and a wasted action at
				// four-a-side is a quarter of the round's output.
				if (Defender->Status != EGF_STATUS::None || Option.TypeEffectiveness <= 0.0f)
				{
					Option.Score = 0.0f;
					Option.Reason = TEXT("status skill, target already afflicted");
					Options.Add(Option);
					continue;
				}

				// Roughly worth a mediocre attack. Enough to be chosen on turn one
				// against a healthy target, not enough to beat a finishing blow.
				Option.Score = FMath::Max(1.0f, Defender->CurrentStats.MaxHP * 0.15f);
				Option.Reason = TEXT("status skill on a clean target");
				Options.Add(Option);
				continue;
			}

			Option.Score = Option.EstimatedDamage;
			Option.Reason = FString::Printf(TEXT("est %.0f dmg, x%.2f effective"),
				Option.EstimatedDamage, Option.TypeEffectiveness);

			if (Profile.Difficulty == EGF_BattleAIDifficulty::Expert)
			{
				// A spread skill is worth what it does to the whole line, not what
				// it does to the seat that happens to be named as its primary.
				if (bSpread)
				{
					float SpreadTotal = 0.0f;
					for (const FGF_BattleSlot& Other : Enemies)
					{
						SpreadTotal += EstimateDamage(Attacker, Board->GetCreatureInSlot(Other), SkillClass, Weather);
					}
					SpreadTotal *= Skill->MultiTargetDamageMultiplier;

					if (SpreadTotal > Option.Score)
					{
						Option.Score = SpreadTotal;
						Option.Reason = FString::Printf(TEXT("spread, est %.0f across %d"), SpreadTotal, Enemies.Num());
					}
				}

				// Removing a creature this round removes a quarter of the enemy's
				// output permanently. Nothing else on the board is worth as much,
				// which is why this bias is a multiplier and not a tiebreak.
				if (Option.EstimatedDamage >= Defender->CurrentStats.CurrentHP)
				{
					Option.bWouldFinish = true;
					Option.Score *= Profile.FinishingBlowBias;
					Option.Reason += TEXT(", FINISHES");
				}
			}

			Options.Add(Option);
		}
	}

	Options.Sort([](const FGF_AIScoredOption& A, const FGF_AIScoredOption& B)
	{
		return A.Score > B.Score;
	});

	return Options;
}

//======================================================================================
// DECIDING
//======================================================================================

FGF_BattleAction UGF_BattleAILibrary::MakeSkillAction(const FGF_BattleSlot& Slot, const UGF_BattleBoard* Board,
	int32 SkillIndex, TSubclassOf<AGF_SkillDefinition> SkillClass, const FGF_BattleSlot& Target)
{
	FGF_BattleAction Action;
	Action.Actor = Slot;
	Action.ActionType = EGF_BattleActionType::Skill;
	Action.SkillIndex = SkillIndex;
	Action.SkillClass = SkillClass;

	if (const AGF_SkillDefinition* Skill = SkillClass ? SkillClass.GetDefaultObject() : nullptr)
	{
		Action.TargetShape = Skill->TargetShape;
	}

	Action.Targets = UGF_TurnOrderLibrary::ExpandTargets(Board, Slot, Action.TargetShape, Target);
	return Action;
}

FGF_BattleAction UGF_BattleAILibrary::MakePass(const FGF_BattleSlot& Slot)
{
	FGF_BattleAction Action;
	Action.Actor = Slot;
	Action.ActionType = EGF_BattleActionType::Pass;
	Action.TargetShape = EGF_SkillTargetShape::Self;
	Action.Targets = { Slot };
	return Action;
}

bool UGF_BattleAILibrary::ConsiderHealItem(const UGF_BattleBoard* Board, const FGF_BattleSlot& Slot,
	const FGF_BattleAIProfile& Profile, FGF_BattleAction& OutAction)
{
	if (Profile.HealItems.Num() == 0)
	{
		return false;
	}

	const AGF_Creature* Self = Board->GetCreatureInSlot(Slot);
	if (Self == nullptr || Self->CurrentStats.MaxHP <= 0.0f)
	{
		return false;
	}

	// Only ever heals ITSELF. An AI that spends its own action healing an ally is
	// a bigger design decision than it looks -- it trades a quarter of the line's
	// output for HP on a seat the player may kill anyway -- so it stays out until
	// it is asked for deliberately.
	if ((Self->CurrentStats.CurrentHP / Self->CurrentStats.MaxHP) > Profile.HealThreshold)
	{
		return false;
	}

	OutAction = FGF_BattleAction();
	OutAction.Actor = Slot;
	OutAction.ActionType = EGF_BattleActionType::Item;
	OutAction.ItemName = Profile.HealItems[0];
	OutAction.TargetShape = EGF_SkillTargetShape::Self;
	OutAction.Targets = { Slot };
	return true;
}

bool UGF_BattleAILibrary::ConsiderSwap(const UGF_BattleBoard* Board, const FGF_BattleSlot& Slot,
	const FGF_BattleAIProfile& Profile, EGF_WeatherType Weather, float CurrentBestScore,
	FGF_BattleAction& OutAction)
{
	if (!Profile.bMaySwap)
	{
		return false;
	}

	const TArray<AGF_Creature*> Bench = Board->GetReserves(Slot.Side);
	const TArray<FGF_BattleSlot> Enemies = Board->GetLivingOpponentsOf(Slot);
	if (Bench.Num() == 0 || Enemies.Num() == 0)
	{
		return false;
	}

	// Is anything on the bench other than the ace still able to fight? If not,
	// the ace is free to come out; if so, it stays locked away.
	bool bNonAceAvailable = false;
	for (int32 i = 0; i < Bench.Num(); ++i)
	{
		if (i != Profile.AceReserveIndex && Bench[i] != nullptr && !Bench[i]->IsDowned())
		{
			bNonAceAvailable = true;
			break;
		}
	}

	int32 BestIndex = INDEX_NONE;
	float BestScore = CurrentBestScore;

	for (int32 i = 0; i < Bench.Num(); ++i)
	{
		AGF_Creature* Candidate = Bench[i];
		if (Candidate == nullptr || Candidate->IsDowned())
		{
			continue;
		}

		if (i == Profile.AceReserveIndex && bNonAceAvailable)
		{
			continue;
		}

		float CandidateBest = 0.0f;
		for (int32 SkillIndex : GetUsableSkillIndices(Candidate))
		{
			for (const FGF_BattleSlot& EnemySlot : Enemies)
			{
				CandidateBest = FMath::Max(CandidateBest,
					EstimateDamage(Candidate, Board->GetCreatureInSlot(EnemySlot), Candidate->Skills[SkillIndex], Weather));
			}
		}

		// A swap costs a whole action and gives the player a free hit on the
		// arrival, so it has to be worth well more than a marginal improvement.
		// 1.75x is the threshold at which the swap pays for itself inside two rounds.
		if (CandidateBest > BestScore * 1.75f)
		{
			BestScore = CandidateBest;
			BestIndex = i;
		}
	}

	if (BestIndex == INDEX_NONE)
	{
		return false;
	}

	OutAction = FGF_BattleAction();
	OutAction.Actor = Slot;
	OutAction.ActionType = EGF_BattleActionType::Swap;
	OutAction.SwapToReserveIndex = BestIndex;
	OutAction.TargetShape = EGF_SkillTargetShape::Self;
	OutAction.Targets = { Slot };
	return true;
}

FGF_BattleAction UGF_BattleAILibrary::DecideAction(
	const UGF_BattleBoard* Board,
	const FGF_BattleSlot& Slot,
	const FGF_BattleAIProfile& Profile,
	EGF_WeatherType Weather)
{
	if (Board == nullptr)
	{
		return MakePass(Slot);
	}

	const AGF_Creature* Self = Board->GetCreatureInSlot(Slot);
	const TArray<FGF_BattleSlot> Enemies = Board->GetLivingOpponentsOf(Slot);

	if (Self == nullptr || Enemies.Num() == 0)
	{
		return MakePass(Slot);
	}

	const TArray<int32> Usable = GetUsableSkillIndices(Self);

	// Every skill spent. Last Resort if one is configured, otherwise a pass --
	// which at least keeps the round moving.
	if (Usable.Num() == 0)
	{
		if (Profile.LastResortSkill)
		{
			// SkillIndex stays INDEX_NONE so no Uses are spent on it.
			return MakeSkillAction(Slot, Board, INDEX_NONE, Profile.LastResortSkill, Enemies[0]);
		}
		return MakePass(Slot);
	}

	// ---- Random: no scoring at all ----------------------------------------
	if (Profile.Difficulty == EGF_BattleAIDifficulty::Random)
	{
		const int32 SkillIndex = Usable[FMath::RandRange(0, Usable.Num() - 1)];
		const FGF_BattleSlot Target = Enemies[FMath::RandRange(0, Enemies.Num() - 1)];
		return MakeSkillAction(Slot, Board, SkillIndex, Self->Skills[SkillIndex], Target);
	}

	// ---- Basic: hardest skill, arbitrary seat ------------------------------
	if (Profile.Difficulty == EGF_BattleAIDifficulty::Basic)
	{
		int32 BestIndex = Usable[0];
		float BestPower = -1.0f;

		for (int32 SkillIndex : Usable)
		{
			const AGF_SkillDefinition* Skill = Self->Skills[SkillIndex].GetDefaultObject();
			if (Skill && Skill->Power > BestPower)
			{
				BestPower = Skill->Power;
				BestIndex = SkillIndex;
			}
		}

		const FGF_BattleSlot Target = Enemies[FMath::RandRange(0, Enemies.Num() - 1)];
		return MakeSkillAction(Slot, Board, BestIndex, Self->Skills[BestIndex], Target);
	}

	// ---- Smart and Expert: score every pair --------------------------------
	TArray<FGF_AIScoredOption> Options = ScoreAllOptions(Board, Slot, Profile, Weather);

	if (Options.Num() == 0)
	{
		return MakePass(Slot);
	}

	if (Profile.Difficulty == EGF_BattleAIDifficulty::Expert)
	{
		FGF_BattleAction ItemAction;
		if (ConsiderHealItem(Board, Slot, Profile, ItemAction))
		{
			return ItemAction;
		}

		FGF_BattleAction SwapAction;
		if (ConsiderSwap(Board, Slot, Profile, Weather, Options[0].Score, SwapAction))
		{
			return SwapAction;
		}
	}

	int32 Chosen = 0;

	// The deliberate wobble. Only ever drops to the SECOND choice -- a tier that
	// occasionally plays its worst option is not a difficulty setting, it is a
	// different AI.
	if (Options.Num() > 1 && Profile.MistakeChance > 0.0f
		&& FMath::FRand() < Profile.MistakeChance)
	{
		Chosen = 1;
	}

	const FGF_AIScoredOption& Pick = Options[Chosen];

	UE_LOG(LogGF_BattleAI, Verbose, TEXT("%s picks skill %d at %s (%s)"),
		*Slot.ToString(), Pick.SkillIndex, *Pick.Target.ToString(), *Pick.Reason);

	return MakeSkillAction(Slot, Board, Pick.SkillIndex, Pick.SkillClass, Pick.Target);
}
