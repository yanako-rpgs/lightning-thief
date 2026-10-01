// CreatureSlotWidget.cpp

#include "GF_CreatureSlotWidget.h"

void UGF_CreatureSlotWidget::SetCreatureData(const FGF_CreatureInstanceData& InCreatureData, bool bIsEmpty)
{
    CreatureData = InCreatureData;
    bIsEmptySlot = bIsEmpty;

    // Trigger Blueprint event
    OnCreatureDataUpdated(CreatureData, bIsEmptySlot);
}

void UGF_CreatureSlotWidget::SetIsSelected(bool bSelected)
{
    if (bIsSelected != bSelected)
    {
        bIsSelected = bSelected;
        OnSelectionChanged(bIsSelected);
    }
}