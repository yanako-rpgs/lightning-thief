// CreaturePanelWidgets.cpp

#include "GF_CreaturePanelWidgets.h"

//====================================================================================
// PARTY PANEL WIDGET
//====================================================================================

void UGF_PartyPanelWidget::InitializeWithController(UGF_VaultController* InVaultController)
{
    VaultController = InVaultController;

    if (!VaultController)
    {
        UE_LOG(LogTemp, Error, TEXT("PartyPanel: Cannot initialize with null PC Controller!"));
        return;
    }

    // Set slot indices
    for (int32 i = 0; i < PartySlots.Num() && i < 6; i++)
    {
        if (PartySlots[i])
        {
            PartySlots[i]->SetSlotIndex(i);
        }
    }

    RefreshAllSlots();
}

void UGF_PartyPanelWidget::RefreshAllSlots()
{
    if (!VaultController)
    {
        return;
    }

    for (int32 i = 0; i < 6; i++)
    {
        RefreshSlot(i);
    }
}

void UGF_PartyPanelWidget::RefreshSlot(int32 SlotIndex)
{
    if (!VaultController)
    {
        return;
    }

    if (SlotIndex < 0 || SlotIndex >= PartySlots.Num())
    {
        return;
    }

    UGF_CreatureSlotWidget* SlotWidget = PartySlots[SlotIndex];
    if (!SlotWidget)
    {
        return;
    }

    // Get Creature data for this slot
    FGF_CreatureInstanceData CreatureData;
    bool bHasCreature = VaultController->GetPartyCreatureData(SlotIndex, CreatureData);

    // Update slot widget
    SlotWidget->SetCreatureData(CreatureData, !bHasCreature);
}

void UGF_PartyPanelWidget::SetSelectedSlot(int32 SlotIndex)
{
    // Deselect old slot
    if (SelectedSlotIndex >= 0 && SelectedSlotIndex < PartySlots.Num())
    {
        if (PartySlots[SelectedSlotIndex])
        {
            PartySlots[SelectedSlotIndex]->SetIsSelected(false);
        }
    }

    // Select new slot
    SelectedSlotIndex = SlotIndex;

    if (SelectedSlotIndex >= 0 && SelectedSlotIndex < PartySlots.Num())
    {
        if (PartySlots[SelectedSlotIndex])
        {
            PartySlots[SelectedSlotIndex]->SetIsSelected(true);
        }
    }
}

//====================================================================================
// BOX PANEL WIDGET
//====================================================================================

void UGF_BoxPanelWidget::InitializeWithController(UGF_VaultController* InVaultController)
{
    VaultController = InVaultController;

    if (!VaultController)
    {
        UE_LOG(LogTemp, Error, TEXT("BoxPanel: Cannot initialize with null PC Controller!"));
        return;
    }

    // Set slot indices
    for (int32 i = 0; i < VaultSlots.Num() && i < 30; i++)
    {
        if (VaultSlots[i])
        {
            VaultSlots[i]->SetSlotIndex(i);
        }
    }

    // Bind to box changed event
    VaultController->OnVaultChanged.AddDynamic(this, &UGF_BoxPanelWidget::OnVaultChanged);

    RefreshAllSlots();
}

void UGF_BoxPanelWidget::RefreshAllSlots()
{
    if (!VaultController)
    {
        return;
    }

    for (int32 i = 0; i < 30; i++)
    {
        RefreshSlot(i);
    }
}

void UGF_BoxPanelWidget::RefreshSlot(int32 SlotIndex)
{
    if (!VaultController)
    {
        return;
    }

    if (SlotIndex < 0 || SlotIndex >= VaultSlots.Num())
    {
        return;
    }

    UGF_CreatureSlotWidget* SlotWidget = VaultSlots[SlotIndex];
    if (!SlotWidget)
    {
        return;
    }

    // Get Creature data for this slot from current box
    FGF_CreatureInstanceData CreatureData;
    bool bHasCreature = VaultController->GetVaultCreatureData(SlotIndex, CreatureData);

    // Update slot widget
    SlotWidget->SetCreatureData(CreatureData, !bHasCreature);
}

void UGF_BoxPanelWidget::OnVaultChanged(int32 NewVaultPageIndex)
{
    // Refresh all slots when box changes
    RefreshAllSlots();

    // Trigger Blueprint event
    OnVaultSwitched(NewVaultPageIndex, GetCurrentVaultPageName());

    UE_LOG(LogTemp, Log, TEXT("BoxPanel: Switched to Vault %d (%s)"),
        NewVaultPageIndex + 1, *GetCurrentVaultPageName());
}

void UGF_BoxPanelWidget::SetSelectedSlot(int32 SlotIndex)
{
    // Deselect old slot
    if (SelectedSlotIndex >= 0 && SelectedSlotIndex < VaultSlots.Num())
    {
        if (VaultSlots[SelectedSlotIndex])
        {
            VaultSlots[SelectedSlotIndex]->SetIsSelected(false);
        }
    }

    // Select new slot
    SelectedSlotIndex = SlotIndex;

    if (SelectedSlotIndex >= 0 && SelectedSlotIndex < VaultSlots.Num())
    {
        if (VaultSlots[SelectedSlotIndex])
        {
            VaultSlots[SelectedSlotIndex]->SetIsSelected(true);
        }
    }
}

int32 UGF_BoxPanelWidget::GetCurrentVaultPageIndex() const
{
    if (!VaultController)
    {
        return 0;
    }

    return VaultController->GetCurrentVaultPageIndex();
}

FString UGF_BoxPanelWidget::GetCurrentVaultPageName() const
{
    if (!VaultController)
    {
        return TEXT("Unknown Vault");
    }

    return VaultController->GetCurrentVaultPageName();
}