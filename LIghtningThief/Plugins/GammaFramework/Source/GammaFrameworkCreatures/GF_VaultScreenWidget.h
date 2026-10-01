// GF_VaultScreenWidget.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GF_VaultController.h"
#include "GF_CreaturePanelWidgets.h"
#include "GF_VaultScreenWidget.generated.h"

/**
 * Which panel is cursor in?
 * IMPORTANT: UENUM must be declared at global scope, NOT inside a class!
 */
UENUM(BlueprintType)
enum class EGF_VaultCursorPanel : uint8
{
    Party   UMETA(DisplayName = "Party"),
    Vault     UMETA(DisplayName = "Vault")
};

/**
 * Main PC Screen Widget
 * Combines Party Panel + Vault Panel + Cursor Logic
 */
UCLASS(Abstract, Blueprintable)
class GAMMAFRAMEWORKCREATURES_API UGF_VaultScreenWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

public:
    //====================================================================================
    // INITIALIZATION
    //====================================================================================

    /**
     * Initialize PC Screen with Creature Manager
     * Call this when opening the PC
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|PC")
    void InitializeVaultScreen();

    /**
     * Close PC Screen
     * Cleans up and saves
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|PC")
    void CloseVaultScreen();

    //====================================================================================
    // WIDGET REFERENCES (Set in Blueprint Designer)
    //====================================================================================

    /**
     * Reference to Party Panel widget
     */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget), Category = "Creature|PC")
    UGF_PartyPanelWidget* PartyPanel;

    /**
     * Reference to Vault Panel widget
     */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget), Category = "Creature|PC")
    UGF_BoxPanelWidget* BoxPanel;

    //====================================================================================
    // PC CONTROLLER
    //====================================================================================

    /**
     * Get PC Controller instance
     */
    UFUNCTION(BlueprintPure, Category = "Creature|PC")
    UGF_VaultController* GetVaultController() const { return VaultController; }

    //====================================================================================
    // CURSOR NAVIGATION
    //====================================================================================

    /**
     * Get current cursor panel
     */
    UFUNCTION(BlueprintPure, Category = "Creature|PC")
    EGF_VaultCursorPanel GetCurrentPanel() const { return CurrentPanel; }

    /**
     * Get current cursor position
     */
    UFUNCTION(BlueprintPure, Category = "Creature|PC")
    int32 GetCursorPosition() const { return CursorPosition; }

    /**
     * Skill cursor (called from input)
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|PC")
    void MoveCursor(int32 DeltaX, int32 DeltaY);

    /**
     * Switch between Party and Vault panels
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|PC")
    void SwitchPanel();

    //====================================================================================
    // GRAB/DROP ACTIONS
    //====================================================================================

    /**
     * Handle A button press (grab/drop Creature)
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|PC")
    void HandleSelectAction();

    /**
     * Handle B button press (cancel/return Creature to source)
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|PC")
    void HandleCancelAction();

    //====================================================================================
    // TRADE MODE
    //====================================================================================

    /**
     * While true, the select button CHOOSES a Creature to trade instead of
     * picking it up to move it. Set it when a trade partner connects, clear it
     * when the trade ends.
     *
     * Grab/drop is suppressed entirely rather than merely ignored: a player
     * halfway through moving a Creature when trade mode turns on would otherwise
     * be left holding one with no way to put it down.
     */
    UPROPERTY(BlueprintReadWrite, Category = "Creature|PC|Trading")
    bool bIsTradeMode = false;

    /**
     * Fires when the player picks a Creature while in trade mode.
     *
     * Implement in Blueprint to show the confirmation dialogue, then pass the
     * same three values straight to UGF_TradeSubsystem::OfferVaultCreature (when
     * bFromVault) or OfferPartyCreature (when not). Remember to call
     * PublishTradeDialogueTokens() before StartDialogue.
     *
     * @param bFromVault   True if the cursor was in the box panel.
     * @param VaultPageIndex   The box the cursor is viewing. Meaningless unless bFromVault.
     * @param SlotIndex  Party slot, or slot within VaultPageIndex.
     * @param Creature    What is actually in that slot, for the prompt text.
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Creature|PC|Trading")
    void OnTradeSlotChosen(bool bFromVault, int32 VaultPageIndex, int32 SlotIndex, const FGF_CreatureInstanceData& Creature);

    /**
     * Where the cursor currently is, in the terms the trade subsystem expects.
     * Useful for highlighting the slot already being offered.
     *
     * @return false if the slot is empty.
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|PC|Trading")
    bool GetCursorSlot(bool& bFromVault, int32& VaultPageIndex, int32& SlotIndex, FGF_CreatureInstanceData& OutCreature) const;

    //====================================================================================
    // BOX SWITCHING
    //====================================================================================

    /**
     * Switch to next box (R button)
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|PC")
    void NextVaultPage();

    /**
     * Switch to previous box (L button)
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|PC")
    void PreviousVaultPage();

    //====================================================================================
    // CURSOR VISUAL (Held Creature)
    //====================================================================================

    /**
     * Get Creature currently held by cursor (for visual display)
     */
    UFUNCTION(BlueprintPure, Category = "Creature|PC")
    FGF_CreatureInstanceData GetHeldCreature() const;

    /**
     * Check if cursor is holding a Creature
     */
    UFUNCTION(BlueprintPure, Category = "Creature|PC")
    bool IsHoldingCreature() const;

    //====================================================================================
    // SELECTED CREATURE INFO (for Stats Display)
    //====================================================================================

    /**
     * Get Creature data for currently selected slot
     * This is what should be shown in the stats panel on the right
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|PC")
    bool GetSelectedCreatureData(FGF_CreatureInstanceData& OutData) const;

    //====================================================================================
    // BLUEPRINT EVENTS (Override in Blueprint)
    //====================================================================================

    /**
     * Called when cursor position changes
     * Override to update visual cursor position
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Creature|PC")
    void OnCursorMoved(EGF_VaultCursorPanel Panel, int32 Position);

    /**
     * Called when Creature is grabbed
     * Override to show Creature following cursor
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Creature|PC")
    void OnCreatureGrabbed(const FGF_CreatureInstanceData& Creature);

    /**
     * Called when Creature is dropped
     * Override to hide Creature cursor
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Creature|PC")
    void OnCreatureDropped();

    /**
     * Called when trying to remove last healthy Creature
     * Override to show warning prompt
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Creature|PC")
    void OnLastCreatureWarning();

    /**
     * Called when box is switched
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Creature|PC")
    void OnVaultSwitched(int32 NewVaultPageIndex, const FString& VaultPageName);

    //====================================================================================
    // UINAVIGATION INTEGRATION (Input handled by UINavigation plugin)
    //====================================================================================

    /**
     * Called by UINavigation when a slot is focused/selected
     * Hook this up to UINavigation's OnNavigate event
     * @param SlotIndex - The slot that was navigated to
     * @param bIsPartySlot - True if party slot, False if box slot
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|PC|UINav")
    void OnSlotNavigated(int32 SlotIndex, bool bIsPartySlot);

    /**
     * Called by UINavigation when Select button pressed (A button)
     * Hook this up to UINavButton's OnSelect event
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|PC|UINav")
    void OnUINavSelect();

    /**
     * Called by UINavigation when Back button pressed (B button)
     * Hook this up to UINavButton's OnReturn event
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|PC|UINav")
    void OnUINavBack();

    /**
     * Called by shoulder buttons for box switching
     * Hook these up to your PlayerController's L/R button events
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|PC|UINav")
    void OnShoulderLeft();

    UFUNCTION(BlueprintCallable, Category = "Creature|PC|UINav")
    void OnShoulderRight();

private:
    // PC Controller instance
    UPROPERTY()
    UGF_VaultController* VaultController = nullptr;

    // Current cursor state
    UPROPERTY()
    EGF_VaultCursorPanel CurrentPanel = EGF_VaultCursorPanel::Party;

    UPROPERTY()
    int32 CursorPosition = 0;

    // Party layout: 6 slots (single column)
    // Vault layout: 30 slots (5 rows x 6 columns)
    static constexpr int32 PartySlots = 6;
    static constexpr int32 VaultRows = 5;
    static constexpr int32 VaultCols = 6;
    static constexpr int32 VaultSlots = VaultRows * VaultCols;

    // Update visual selection
    void UpdateCursorVisual();

    // Clamp cursor position to valid range
    void ClampCursorPosition();

    // Event handlers
    UFUNCTION()
    void OnVaultCreatureGrabbed(FGF_VaultCursorState CursorState);

    UFUNCTION()
    void OnVaultCreatureDropped();

    UFUNCTION()
    void OnVaultPageChanged(int32 NewVaultPageIndex);
};