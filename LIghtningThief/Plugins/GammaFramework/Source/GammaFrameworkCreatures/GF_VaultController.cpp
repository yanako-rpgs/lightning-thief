// GF_VaultController.cpp

#include "GF_VaultController.h"
#include "GF_CreatureManagerSubsystem.h"

//====================================================================================
// INITIALIZATION
//====================================================================================

void UGF_VaultController::InitializeWithManager(UGF_CreatureManagerSubsystem* InCreatureManager)
{
    CreatureManager = InCreatureManager;

    if (!CreatureManager)
    {
        UE_LOG(LogTemp, Error, TEXT("VaultController: CreatureManager is null!"));
        return;
    }

    // Initialize cursor state
    CursorState.bIsHolding = false;
    CursorState.SourceLocation = EGF_VaultCursorSource::None;
    CursorState.SourceIndex = -1;

    // Start with first box
    CurrentVaultPageIndex = 0;

    UE_LOG(LogTemp, Log, TEXT("VaultController: Initialized successfully"));
}

//====================================================================================
// GRAB/DROP OPERATIONS
//====================================================================================

bool UGF_VaultController::GrabFromParty(int32 SlotIndex)
{
    if (!CreatureManager)
    {
        UE_LOG(LogTemp, Error, TEXT("VaultController: CreatureManager is null!"));
        return false;
    }

    // Can't grab if already holding
    if (CursorState.bIsHolding)
    {
        UE_LOG(LogTemp, Warning, TEXT("VaultController: Already holding a Creature!"));
        return false;
    }

    // Get Creature data from party
    FGF_CreatureInstanceData CreatureData;
    if (!CreatureManager->GetPartyCreatureData(SlotIndex, CreatureData))
    {
        UE_LOG(LogTemp, Warning, TEXT("VaultController: No Creature at party slot %d"), SlotIndex);
        return false;
    }

    // Check if would remove last healthy Creature
    if (WouldRemoveLastHealthyCreature(SlotIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("VaultController: Cannot grab last healthy Creature from party!"));
        return false;
    }

    // Grab the Creature
    CursorState.bIsHolding = true;
    CursorState.SourceLocation = EGF_VaultCursorSource::Party;
    CursorState.SourceIndex = SlotIndex;
    CursorState.HeldCreature = CreatureData;

    UE_LOG(LogTemp, Log, TEXT("VaultController: Grabbed %s from party slot %d"),
        *CreatureData.GetDisplayName().ToString(), SlotIndex);

    // Broadcast event
    OnCreatureGrabbed.Broadcast(CursorState);

    return true;
}

bool UGF_VaultController::GrabFromVault(int32 SlotIndex)
{
    if (!CreatureManager)
    {
        UE_LOG(LogTemp, Error, TEXT("VaultController: CreatureManager is null!"));
        return false;
    }

    // Can't grab if already holding
    if (CursorState.bIsHolding)
    {
        UE_LOG(LogTemp, Warning, TEXT("VaultController: Already holding a Creature!"));
        return false;
    }

    // Get Creature data from current box
    FGF_CreatureInstanceData CreatureData;
    if (!CreatureManager->GetVaultCreatureData(CurrentVaultPageIndex, SlotIndex, CreatureData))
    {
        UE_LOG(LogTemp, Warning, TEXT("VaultController: No Creature at box %d slot %d"),
            CurrentVaultPageIndex, SlotIndex);
        return false;
    }

    // Grab the Creature
    CursorState.bIsHolding = true;
    CursorState.SourceLocation = EGF_VaultCursorSource::Vault;
    CursorState.SourceIndex = SlotIndex;
    CursorState.HeldCreature = CreatureData;

    UE_LOG(LogTemp, Log, TEXT("VaultController: Grabbed %s from box %d slot %d"),
        *CreatureData.GetDisplayName().ToString(), CurrentVaultPageIndex, SlotIndex);

    // Broadcast event
    OnCreatureGrabbed.Broadcast(CursorState);

    return true;
}

bool UGF_VaultController::DropToParty(int32 SlotIndex)
{
    if (!CreatureManager)
    {
        UE_LOG(LogTemp, Error, TEXT("VaultController: CreatureManager is null!"));
        return false;
    }

    // Must be holding a Creature
    if (!CursorState.bIsHolding)
    {
        UE_LOG(LogTemp, Warning, TEXT("VaultController: Not holding any Creature!"));
        return false;
    }

    // Check if target slot has a Creature (swap) or is empty (place)
    FGF_CreatureInstanceData TargetCreature;
    bool bTargetHasCreature = CreatureManager->GetPartyCreatureData(SlotIndex, TargetCreature);

    if (bTargetHasCreature)
    {
        // SWAP: Target has Creature, swap with held Creature
        SwapCreature(CursorState.SourceLocation, CursorState.SourceIndex,
                    EGF_VaultCursorSource::Party, SlotIndex);
    }
    else
    {
        // PLACE: Target is empty, just place held Creature
        if (CursorState.SourceLocation == EGF_VaultCursorSource::Party)
        {
            // Moving within party
            CreatureManager->SwapPartyCreature(CursorState.SourceIndex, SlotIndex);
        }
        else if (CursorState.SourceLocation == EGF_VaultCursorSource::Vault)
        {
            // Moving from box to party
            if (CreatureManager->VaultSystem)
            {
                CreatureManager->VaultSystem->RemoveFromVault(CurrentVaultPageIndex, CursorState.SourceIndex);
            }
            CreatureManager->AddToParty(CursorState.HeldCreature);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("VaultController: Dropped %s to party slot %d"),
        *CursorState.HeldCreature.GetDisplayName().ToString(), SlotIndex);

    // Clear cursor
    CursorState.bIsHolding = false;
    CursorState.SourceLocation = EGF_VaultCursorSource::None;
    CursorState.SourceIndex = -1;

    // Broadcast event
    OnCreatureDropped.Broadcast();

    return true;
}

bool UGF_VaultController::DropToVault(int32 SlotIndex)
{
    if (!CreatureManager)
    {
        UE_LOG(LogTemp, Error, TEXT("VaultController: CreatureManager is null!"));
        return false;
    }

    // Must be holding a Creature
    if (!CursorState.bIsHolding)
    {
        UE_LOG(LogTemp, Warning, TEXT("VaultController: Not holding any Creature!"));
        return false;
    }

    // Check if target slot has a Creature (swap) or is empty (place)
    FGF_CreatureInstanceData TargetCreature;
    bool bTargetHasCreature = CreatureManager->GetVaultCreatureData(CurrentVaultPageIndex, SlotIndex, TargetCreature);

    if (bTargetHasCreature)
    {
        // SWAP: Target has Creature, swap with held Creature
        SwapCreature(CursorState.SourceLocation, CursorState.SourceIndex,
                    EGF_VaultCursorSource::Vault, SlotIndex);
    }
    else
    {
        // PLACE: Target is empty, just place held Creature
        if (CursorState.SourceLocation == EGF_VaultCursorSource::Party)
        {
            // Moving from party to box
            CreatureManager->RemoveFromParty(CursorState.SourceIndex);
            if (CreatureManager->VaultSystem)
            {
                CreatureManager->VaultSystem->AddToVault(CurrentVaultPageIndex, CursorState.HeldCreature);
            }
        }
        else if (CursorState.SourceLocation == EGF_VaultCursorSource::Vault)
        {
            // Moving within box (same box or different box)
            if (CreatureManager->VaultSystem)
            {
                CreatureManager->VaultSystem->RemoveFromVault(CurrentVaultPageIndex, CursorState.SourceIndex);
                CreatureManager->VaultSystem->AddToVault(CurrentVaultPageIndex, CursorState.HeldCreature);
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("VaultController: Dropped %s to box %d slot %d"),
        *CursorState.HeldCreature.GetDisplayName().ToString(), CurrentVaultPageIndex, SlotIndex);

    // Clear cursor
    CursorState.bIsHolding = false;
    CursorState.SourceLocation = EGF_VaultCursorSource::None;
    CursorState.SourceIndex = -1;

    // Broadcast event
    OnCreatureDropped.Broadcast();

    return true;
}

bool UGF_VaultController::CanDropToParty(int32 SlotIndex) const
{
    if (!CreatureManager)
    {
        return false;
    }

    // Not holding anything - nothing to drop
    if (!CursorState.bIsHolding)
    {
        return false;
    }

    // If source is party, check if this would remove last healthy Creature
    if (CursorState.SourceLocation == EGF_VaultCursorSource::Party)
    {
        // Get target slot info
        FGF_CreatureInstanceData TargetCreature;
        bool bTargetHasCreature = CreatureManager->GetPartyCreatureData(SlotIndex, TargetCreature);

        if (!bTargetHasCreature)
        {
            // Dropping to empty slot - this moves Creature within party, always OK
            return true;
        }
        else
        {
            // Swapping with another party member
            // Check if we'd have at least one healthy Creature after swap
            return !WouldRemoveLastHealthyCreature(CursorState.SourceIndex);
        }
    }

    // If source is box, always allow (adding to party)
    return true;
}

void UGF_VaultController::ClearCursor()
{
    if (!CursorState.bIsHolding)
    {
        return; // Nothing to clear
    }

    UE_LOG(LogTemp, Log, TEXT("VaultController: Clearing cursor, returning %s to source"),
        *CursorState.HeldCreature.GetDisplayName().ToString());

    // Creature is automatically still in its original location
    // We just clear the cursor state
    CursorState.bIsHolding = false;
    CursorState.SourceLocation = EGF_VaultCursorSource::None;
    CursorState.SourceIndex = -1;

    // Broadcast event
    OnCreatureDropped.Broadcast();
}

//====================================================================================
// BOX MANAGEMENT
//====================================================================================

FString UGF_VaultController::GetCurrentVaultPageName() const
{
    if (!CreatureManager || !CreatureManager->VaultSystem)
    {
        return TEXT("Unknown Vault");
    }

    return CreatureManager->VaultSystem->GetVaultPageName(CurrentVaultPageIndex);
}

void UGF_VaultController::NextVaultPage()
{
    if (!CreatureManager || !CreatureManager->VaultSystem)
    {
        return;
    }

    int32 MaxVaultPages = CreatureManager->VaultSystem->VaultPages.Num();
    CurrentVaultPageIndex = (CurrentVaultPageIndex + 1) % MaxVaultPages;

    UE_LOG(LogTemp, Log, TEXT("VaultController: Switched to Vault %d (%s)"),
        CurrentVaultPageIndex + 1, *GetCurrentVaultPageName());

    // Broadcast event
    OnVaultChanged.Broadcast(CurrentVaultPageIndex);
}

void UGF_VaultController::PreviousVaultPage()
{
    if (!CreatureManager || !CreatureManager->VaultSystem)
    {
        return;
    }

    int32 MaxVaultPages = CreatureManager->VaultSystem->VaultPages.Num();
    CurrentVaultPageIndex = (CurrentVaultPageIndex - 1 + MaxVaultPages) % MaxVaultPages;

    UE_LOG(LogTemp, Log, TEXT("VaultController: Switched to Vault %d (%s)"),
        CurrentVaultPageIndex + 1, *GetCurrentVaultPageName());

    // Broadcast event
    OnVaultChanged.Broadcast(CurrentVaultPageIndex);
}

void UGF_VaultController::SetCurrentVaultPage(int32 VaultPageIndex)
{
    if (!CreatureManager || !CreatureManager->VaultSystem)
    {
        return;
    }

    int32 MaxVaultPages = CreatureManager->VaultSystem->VaultPages.Num();
    if (VaultPageIndex < 0 || VaultPageIndex >= MaxVaultPages)
    {
        UE_LOG(LogTemp, Warning, TEXT("VaultController: Invalid box index %d"), VaultPageIndex);
        return;
    }

    CurrentVaultPageIndex = VaultPageIndex;

    UE_LOG(LogTemp, Log, TEXT("VaultController: Set current box to %d (%s)"),
        CurrentVaultPageIndex + 1, *GetCurrentVaultPageName());

    // Broadcast event
    OnVaultChanged.Broadcast(CurrentVaultPageIndex);
}

//====================================================================================
// DATA ACCESS
//====================================================================================

bool UGF_VaultController::GetPartyCreatureData(int32 SlotIndex, FGF_CreatureInstanceData& OutData) const
{
    if (!CreatureManager)
    {
        return false;
    }

    return CreatureManager->GetPartyCreatureData(SlotIndex, OutData);
}

bool UGF_VaultController::GetVaultCreatureData(int32 SlotIndex, FGF_CreatureInstanceData& OutData) const
{
    if (!CreatureManager)
    {
        return false;
    }

    return CreatureManager->GetVaultCreatureData(CurrentVaultPageIndex, SlotIndex, OutData);
}

//====================================================================================
// HELPER FUNCTIONS
//====================================================================================

bool UGF_VaultController::WouldRemoveLastHealthyCreature(int32 PartySlotIndex) const
{
    if (!CreatureManager || !CreatureManager->VaultSystem)
    {
        return false;
    }

    // Count healthy Creature in party
    int32 HealthyCount = 0;
    for (int32 i = 0; i < CreatureManager->GetPartySize(); i++)
    {
        FGF_CreatureInstanceData Data;
        if (CreatureManager->GetPartyCreatureData(i, Data))
        {
            if (!Data.bIsDowned && Data.CurrentHP > 0)
            {
                HealthyCount++;
            }
        }
    }

    // If removing this Creature would leave us with 0 healthy Creature, block it
    if (HealthyCount <= 1)
    {
        // Check if the Creature we're trying to remove is healthy
        FGF_CreatureInstanceData RemovingData;
        if (CreatureManager->GetPartyCreatureData(PartySlotIndex, RemovingData))
        {
            if (!RemovingData.bIsDowned && RemovingData.CurrentHP > 0)
            {
                return true; // Would remove last healthy Creature
            }
        }
    }

    return false;
}

void UGF_VaultController::SwapCreature(EGF_VaultCursorSource SourceLocation, int32 SourceIndex,
                                       EGF_VaultCursorSource TargetLocation, int32 TargetIndex)
{
    if (!CreatureManager)
    {
        return;
    }

    // Get both Creature data
    FGF_CreatureInstanceData SourceCreature = CursorState.HeldCreature;
    FGF_CreatureInstanceData TargetCreature;

    // Get target Creature
    if (TargetLocation == EGF_VaultCursorSource::Party)
    {
        CreatureManager->GetPartyCreatureData(TargetIndex, TargetCreature);
    }
    else if (TargetLocation == EGF_VaultCursorSource::Vault)
    {
        CreatureManager->GetVaultCreatureData(CurrentVaultPageIndex, TargetIndex, TargetCreature);
    }

    // Remove both Creature from their current locations
    if (SourceLocation == EGF_VaultCursorSource::Party)
    {
        CreatureManager->RemoveFromParty(SourceIndex);
    }
    else if (SourceLocation == EGF_VaultCursorSource::Vault)
    {
        if (CreatureManager->VaultSystem)
        {
            CreatureManager->VaultSystem->RemoveFromVault(CurrentVaultPageIndex, SourceIndex);
        }
    }

    if (TargetLocation == EGF_VaultCursorSource::Party)
    {
        CreatureManager->RemoveFromParty(TargetIndex);
    }
    else if (TargetLocation == EGF_VaultCursorSource::Vault)
    {
        if (CreatureManager->VaultSystem)
        {
            CreatureManager->VaultSystem->RemoveFromVault(CurrentVaultPageIndex, TargetIndex);
        }
    }

    // Place Creature in swapped locations
    if (TargetLocation == EGF_VaultCursorSource::Party)
    {
        // Insert at specific index (this might need custom implementation in VaultSystem)
        CreatureManager->AddToParty(SourceCreature);
    }
    else if (TargetLocation == EGF_VaultCursorSource::Vault)
    {
        if (CreatureManager->VaultSystem)
        {
            CreatureManager->VaultSystem->AddToVault(CurrentVaultPageIndex, SourceCreature);
        }
    }

    if (SourceLocation == EGF_VaultCursorSource::Party)
    {
        CreatureManager->AddToParty(TargetCreature);
    }
    else if (SourceLocation == EGF_VaultCursorSource::Vault)
    {
        if (CreatureManager->VaultSystem)
        {
            CreatureManager->VaultSystem->AddToVault(CurrentVaultPageIndex, TargetCreature);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("VaultController: Swapped %s <-> %s"),
        *SourceCreature.GetDisplayName().ToString(),
        *TargetCreature.GetDisplayName().ToString());
}