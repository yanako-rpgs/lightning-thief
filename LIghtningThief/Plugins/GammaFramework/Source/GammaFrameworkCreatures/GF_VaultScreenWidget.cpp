// GF_VaultScreenWidget.cpp

#include "GF_VaultScreenWidget.h"
#include "GF_CreatureManagerSubsystem.h"
#include "Kismet/GameplayStatics.h"

void UGF_VaultScreenWidget::NativeConstruct()
{
    Super::NativeConstruct();

    InitializeVaultScreen();
    // No input setup needed - UINavigation handles it!
}

void UGF_VaultScreenWidget::NativeDestruct()
{
    // No input cleanup needed - UINavigation handles it!
    Super::NativeDestruct();
}

//====================================================================================
// INITIALIZATION
//====================================================================================

void UGF_VaultScreenWidget::InitializeVaultScreen()
{
    // Get Creature Manager
    UGameInstance* GameInstance = GetGameInstance();
    if (!GameInstance)
    {
        UE_LOG(LogTemp, Error, TEXT("VaultScreen: Cannot get GameInstance!"));
        return;
    }

    UGF_CreatureManagerSubsystem* CreatureManager = GameInstance->GetSubsystem<UGF_CreatureManagerSubsystem>();
    if (!CreatureManager)
    {
        UE_LOG(LogTemp, Error, TEXT("VaultScreen: Cannot get CreatureManager!"));
        return;
    }

    // Create PC Controller
    VaultController = NewObject<UGF_VaultController>(this);
    if (!VaultController)
    {
        UE_LOG(LogTemp, Error, TEXT("VaultScreen: Failed to create PC Controller!"));
        return;
    }

    VaultController->InitializeWithManager(CreatureManager);

    // Bind to PC Controller events
    VaultController->OnCreatureGrabbed.AddDynamic(this, &UGF_VaultScreenWidget::OnVaultCreatureGrabbed);
    VaultController->OnCreatureDropped.AddDynamic(this, &UGF_VaultScreenWidget::OnVaultCreatureDropped);
    VaultController->OnVaultChanged.AddDynamic(this, &UGF_VaultScreenWidget::OnVaultPageChanged);

    // Initialize panels with renamed function
    if (PartyPanel)
    {
        PartyPanel->InitializeWithController(VaultController);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("VaultScreen: PartyPanel is null!"));
    }

    if (BoxPanel)
    {
        BoxPanel->InitializeWithController(VaultController);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("VaultScreen: BoxPanel is null!"));
    }

    // Start with cursor in party
    CurrentPanel = EGF_VaultCursorPanel::Party;
    CursorPosition = 0;
    UpdateCursorVisual();

    UE_LOG(LogTemp, Log, TEXT("VaultScreen: Initialized successfully"));
}

void UGF_VaultScreenWidget::CloseVaultScreen()
{
    // Clear any held Creature
    if (VaultController && VaultController->IsHoldingCreature())
    {
        VaultController->ClearCursor();
    }

    // Mark dirty instead of saving — the player must manually save.
    // Auto-saving here would ruin nuzlocke/unique-hunt runs that rely on soft-resetting.
    UGameInstance* GameInstance = GetGameInstance();
    if (GameInstance)
    {
        UGF_CreatureManagerSubsystem* CreatureManager = GameInstance->GetSubsystem<UGF_CreatureManagerSubsystem>();
        if (CreatureManager)
        {
            CreatureManager->MarkDirty();
        }
    }

    RemoveFromParent();

    UE_LOG(LogTemp, Log, TEXT("VaultScreen: Closed"));
}

//====================================================================================
// UINAVIGATION INTEGRATION
//====================================================================================

void UGF_VaultScreenWidget::OnSlotNavigated(int32 SlotIndex, bool bIsPartySlot)
{
    // Update which panel and position based on UINavigation focus
    if (bIsPartySlot)
    {
        CurrentPanel = EGF_VaultCursorPanel::Party;
        CursorPosition = SlotIndex;
    }
    else
    {
        CurrentPanel = EGF_VaultCursorPanel::Vault;
        CursorPosition = SlotIndex;
    }

    UpdateCursorVisual();

    UE_LOG(LogTemp, Log, TEXT("VaultScreen: Navigated to %s slot %d"),
        bIsPartySlot ? TEXT("Party") : TEXT("Vault"), SlotIndex);
}

void UGF_VaultScreenWidget::OnUINavSelect()
{
    // Called when player presses A button (UINavigation's Select event)
    HandleSelectAction();
}

void UGF_VaultScreenWidget::OnUINavBack()
{
    // Called when player presses B button (UINavigation's Return event)
    if (VaultController && VaultController->IsHoldingCreature())
    {
        HandleCancelAction();
    }
    else
    {
        CloseVaultScreen();
    }
}

void UGF_VaultScreenWidget::OnShoulderLeft()
{
    // Called from PlayerController's L button event
    PreviousVaultPage();
}

void UGF_VaultScreenWidget::OnShoulderRight()
{
    // Called from PlayerController's R button event
    NextVaultPage();
}

//====================================================================================
// CURSOR NAVIGATION (Optional - UINavigation handles this automatically)
//====================================================================================

void UGF_VaultScreenWidget::MoveCursor(int32 DeltaX, int32 DeltaY)
{
    // NOTE: With UINavigation, cursor movement is handled automatically
    // This function is kept for backwards compatibility but not needed
    UE_LOG(LogTemp, Warning, TEXT("MoveCursor called - but UINavigation should handle movement!"));
}

void UGF_VaultScreenWidget::SwitchPanel()
{
    // NOTE: With UINavigation, panel switching is handled by navigation graph
    // This function is kept for backwards compatibility but not needed
    UE_LOG(LogTemp, Warning, TEXT("SwitchPanel called - but UINavigation should handle panel switching!"));
}

void UGF_VaultScreenWidget::UpdateCursorVisual()
{
    // Update panel selections
    if (PartyPanel)
    {
        if (CurrentPanel == EGF_VaultCursorPanel::Party)
        {
            PartyPanel->SetSelectedSlot(CursorPosition);
        }
        else
        {
            PartyPanel->SetSelectedSlot(-1); // Deselect
        }
    }

    if (BoxPanel)
    {
        if (CurrentPanel == EGF_VaultCursorPanel::Vault)
        {
            BoxPanel->SetSelectedSlot(CursorPosition);
        }
        else
        {
            BoxPanel->SetSelectedSlot(-1); // Deselect
        }
    }

    // Trigger Blueprint event
    OnCursorMoved(CurrentPanel, CursorPosition);
}

//====================================================================================
// GRAB/DROP ACTIONS
//====================================================================================

bool UGF_VaultScreenWidget::GetCursorSlot(bool& bFromVault, int32& VaultPageIndex, int32& SlotIndex,
    FGF_CreatureInstanceData& OutCreature) const
{
    bFromVault = (CurrentPanel == EGF_VaultCursorPanel::Vault);
    VaultPageIndex = VaultController ? VaultController->GetCurrentVaultPageIndex() : INDEX_NONE;
    SlotIndex = CursorPosition;

    return GetSelectedCreatureData(OutCreature);
}

void UGF_VaultScreenWidget::HandleSelectAction()
{
    if (!VaultController)
    {
        return;
    }

    // Trade mode takes the select button over completely. Checked before the
    // holding branch so a Creature picked up before trade mode began cannot be
    // dropped into a slot the trade is about to overwrite.
    if (bIsTradeMode)
    {
        bool bFromVault = false;
        int32 VaultPageIndex = INDEX_NONE;
        int32 SlotIndex = INDEX_NONE;
        FGF_CreatureInstanceData Chosen;

        if (GetCursorSlot(bFromVault, VaultPageIndex, SlotIndex, Chosen))
        {
            OnTradeSlotChosen(bFromVault, VaultPageIndex, SlotIndex, Chosen);
        }

        return;
    }

    if (VaultController->IsHoldingCreature())
    {
        // DROP Creature
        bool bSuccess = false;

        if (CurrentPanel == EGF_VaultCursorPanel::Party)
        {
            // Check if would remove last healthy Creature
            if (!VaultController->CanDropToParty(CursorPosition))
            {
                OnLastCreatureWarning();
                return;
            }

            bSuccess = VaultController->DropToParty(CursorPosition);
        }
        else if (CurrentPanel == EGF_VaultCursorPanel::Vault)
        {
            bSuccess = VaultController->DropToVault(CursorPosition);
        }

        if (bSuccess)
        {
            // Refresh displays
            if (PartyPanel)
            {
                PartyPanel->RefreshAllSlots();
            }
            if (BoxPanel)
            {
                BoxPanel->RefreshAllSlots();
            }
        }
    }
    else
    {
        // GRAB Creature
        bool bSuccess = false;

        if (CurrentPanel == EGF_VaultCursorPanel::Party)
        {
            bSuccess = VaultController->GrabFromParty(CursorPosition);
        }
        else if (CurrentPanel == EGF_VaultCursorPanel::Vault)
        {
            bSuccess = VaultController->GrabFromVault(CursorPosition);
        }

        if (bSuccess)
        {
            // Refresh displays
            if (PartyPanel)
            {
                PartyPanel->RefreshAllSlots();
            }
            if (BoxPanel)
            {
                BoxPanel->RefreshAllSlots();
            }
        }
    }
}

void UGF_VaultScreenWidget::HandleCancelAction()
{
    if (!VaultController)
    {
        return;
    }

    // Return Creature to source location
    VaultController->ClearCursor();

    // Refresh displays
    if (PartyPanel)
    {
        PartyPanel->RefreshAllSlots();
    }
    if (BoxPanel)
    {
        BoxPanel->RefreshAllSlots();
    }
}

//====================================================================================
// BOX SWITCHING
//====================================================================================

void UGF_VaultScreenWidget::NextVaultPage()
{
    if (VaultController)
    {
        VaultController->NextVaultPage();
    }
}

void UGF_VaultScreenWidget::PreviousVaultPage()
{
    if (VaultController)
    {
        VaultController->PreviousVaultPage();
    }
}

//====================================================================================
// DATA ACCESS
//====================================================================================

FGF_CreatureInstanceData UGF_VaultScreenWidget::GetHeldCreature() const
{
    if (VaultController)
    {
        return VaultController->GetHeldCreature();
    }

    return FGF_CreatureInstanceData();
}

bool UGF_VaultScreenWidget::IsHoldingCreature() const
{
    if (VaultController)
    {
        return VaultController->IsHoldingCreature();
    }

    return false;
}

bool UGF_VaultScreenWidget::GetSelectedCreatureData(FGF_CreatureInstanceData& OutData) const
{
    if (!VaultController)
    {
        return false;
    }

    if (CurrentPanel == EGF_VaultCursorPanel::Party)
    {
        return VaultController->GetPartyCreatureData(CursorPosition, OutData);
    }
    else if (CurrentPanel == EGF_VaultCursorPanel::Vault)
    {
        return VaultController->GetVaultCreatureData(CursorPosition, OutData);
    }

    return false;
}

//====================================================================================
// EVENT HANDLERS
//====================================================================================

void UGF_VaultScreenWidget::OnVaultCreatureGrabbed(FGF_VaultCursorState CursorState)
{
    OnCreatureGrabbed(CursorState.HeldCreature);
}

void UGF_VaultScreenWidget::OnVaultCreatureDropped()
{
    OnCreatureDropped();
}

void UGF_VaultScreenWidget::OnVaultPageChanged(int32 NewVaultPageIndex)
{
    if (VaultController)
    {
        OnVaultSwitched(NewVaultPageIndex, VaultController->GetCurrentVaultPageName());
    }
}