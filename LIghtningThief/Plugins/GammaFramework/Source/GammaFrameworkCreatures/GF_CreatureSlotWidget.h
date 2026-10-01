// CreatureSlotWidget.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GF_CreatureInstanceData.h"
#include "GF_CreatureSlotWidget.generated.h"

/**
 * Base widget class for a single Creature slot in PC
 * Extend this in Blueprint to create your visual design
 */
UCLASS(Abstract, Blueprintable)
class GAMMAFRAMEWORKCREATURES_API UGF_CreatureSlotWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    //====================================================================================
    // SLOT DATA
    //====================================================================================

    /**
     * Set the Creature data for this slot
     * Call this to update the slot's display
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|Slot")
    void SetCreatureData(const FGF_CreatureInstanceData& InCreatureData, bool bIsEmpty);

    /**
     * Get the Creature data this slot is displaying
     */
    UFUNCTION(BlueprintPure, Category = "Creature|Slot")
    FGF_CreatureInstanceData GetCreatureData() const { return CreatureData; }

    /**
     * Check if this slot is empty
     */
    UFUNCTION(BlueprintPure, Category = "Creature|Slot")
    bool IsEmpty() const { return bIsEmptySlot; }

    /**
     * Set slot index (for reference)
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|Slot")
    void SetSlotIndex(int32 Index) { SlotIndex = Index; }

    /**
     * Get slot index
     */
    UFUNCTION(BlueprintPure, Category = "Creature|Slot")
    int32 GetSlotIndex() const { return SlotIndex; }

    //====================================================================================
    // VISUAL STATE
    //====================================================================================

    /**
     * Set whether this slot should show as selected/hovered
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|Slot")
    void SetIsSelected(bool bSelected);

    /**
     * Check if this slot is selected
     */
    UFUNCTION(BlueprintPure, Category = "Creature|Slot")
    bool IsSelected() const { return bIsSelected; }

    //====================================================================================
    // BLUEPRINT EVENTS (Override these in your Blueprint)
    //====================================================================================

    /**
     * Called when Creature data is updated
     * Override in Blueprint to update your UI elements
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Creature|Slot")
    void OnCreatureDataUpdated(const FGF_CreatureInstanceData& Data, bool bEmpty);

    /**
     * Called when selection state changes
     * Override in Blueprint to show/hide selection highlight
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Creature|Slot")
    void OnSelectionChanged(bool bSelected);

protected:
    // The Creature data this slot is displaying
    UPROPERTY(BlueprintReadOnly, Category = "Creature|Slot")
    FGF_CreatureInstanceData CreatureData;

    // Is this slot empty?
    UPROPERTY(BlueprintReadOnly, Category = "Creature|Slot")
    bool bIsEmptySlot = true;

    // Is this slot selected/hovered?
    UPROPERTY(BlueprintReadOnly, Category = "Creature|Slot")
    bool bIsSelected = false;

    // Index of this slot (0-5 for party, 0-29 for box)
    UPROPERTY(BlueprintReadOnly, Category = "Creature|Slot")
    int32 SlotIndex = 0;
};