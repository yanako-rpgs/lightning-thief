// CreaturePanelWidgets.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GF_CreatureSlotWidget.h"
#include "GF_VaultController.h"
#include "GF_CreaturePanelWidgets.generated.h"

/**
 * Base widget for Party Panel (6 slots)
 */
UCLASS(Abstract, Blueprintable)
class GAMMAFRAMEWORKCREATURES_API UGF_PartyPanelWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    //====================================================================================
    // INITIALIZATION
    //====================================================================================

    /**
     * Initialize with PC Controller reference
     * NOTE: Renamed to avoid conflict with UUserWidget::Initialize()
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|Party")
    void InitializeWithController(UGF_VaultController* InVaultController);

    /**
     * Refresh all party slots from PC Controller
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|Party")
    void RefreshAllSlots();

    /**
     * Refresh a specific party slot
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|Party")
    void RefreshSlot(int32 SlotIndex);

    //====================================================================================
    // SLOT WIDGETS (Set these in Blueprint)
    //====================================================================================

    /**
     * Array of party slot widgets (size 6)
     * Set these in your Blueprint Widget Designer
     */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget), Category = "Creature|Party")
    TArray<UGF_CreatureSlotWidget*> PartySlots;

    //====================================================================================
    // SELECTION
    //====================================================================================

    /**
     * Set which party slot is selected
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|Party")
    void SetSelectedSlot(int32 SlotIndex);

    /**
     * Get currently selected slot index
     */
    UFUNCTION(BlueprintPure, Category = "Creature|Party")
    int32 GetSelectedSlot() const { return SelectedSlotIndex; }

protected:
    UPROPERTY()
    UGF_VaultController* VaultController = nullptr;

    UPROPERTY(BlueprintReadOnly, Category = "Creature|Party")
    int32 SelectedSlotIndex = -1;
};

/**
 * Base widget for Vault Panel (30 slots in 5x6 grid)
 */
UCLASS(Abstract, Blueprintable)
class GAMMAFRAMEWORKCREATURES_API UGF_BoxPanelWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    //====================================================================================
    // INITIALIZATION
    //====================================================================================

    /**
     * Initialize with PC Controller reference
     * NOTE: Renamed to avoid conflict with UUserWidget::Initialize()
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
    void InitializeWithController(UGF_VaultController* InVaultController);

    /**
     * Refresh all box slots from PC Controller
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
    void RefreshAllSlots();

    /**
     * Refresh a specific box slot
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
    void RefreshSlot(int32 SlotIndex);

    /**
     * Called when box changes (L/R button)
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
    void OnVaultChanged(int32 NewVaultPageIndex);

    //====================================================================================
    // SLOT WIDGETS (Set these in Blueprint)
    //====================================================================================

    /**
     * Array of box slot widgets (size 30)
     * Set these in your Blueprint Widget Designer
     */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget), Category = "Creature|Vault")
    TArray<UGF_CreatureSlotWidget*> VaultSlots;

    //====================================================================================
    // SELECTION
    //====================================================================================

    /**
     * Set which box slot is selected
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|Vault")
    void SetSelectedSlot(int32 SlotIndex);

    /**
     * Get currently selected slot index
     */
    UFUNCTION(BlueprintPure, Category = "Creature|Vault")
    int32 GetSelectedSlot() const { return SelectedSlotIndex; }

    //====================================================================================
    // BOX INFO
    //====================================================================================

    /**
     * Get current box index
     */
    UFUNCTION(BlueprintPure, Category = "Creature|Vault")
    int32 GetCurrentVaultPageIndex() const;

    /**
     * Get current box name
     */
    UFUNCTION(BlueprintPure, Category = "Creature|Vault")
    FString GetCurrentVaultPageName() const;

    //====================================================================================
    // BLUEPRINT EVENTS
    //====================================================================================

    /**
     * Called when box is switched (override in Blueprint to update UI)
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Creature|Vault")
    void OnVaultSwitched(int32 NewVaultPageIndex, const FString& VaultPageName);

protected:
    UPROPERTY()
    UGF_VaultController* VaultController = nullptr;

    UPROPERTY(BlueprintReadOnly, Category = "Creature|Vault")
    int32 SelectedSlotIndex = -1;
};