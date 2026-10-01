// GF_VaultController.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "GF_CreatureInstanceData.h"
#include "GF_VaultController.generated.h"

// Forward declarations
class UGF_CreatureManagerSubsystem;

/**
 * Where did cursor grab Creature from?
 * IMPORTANT: UENUM must be declared at global scope, NOT inside a class!
 */
UENUM(BlueprintType)
enum class EGF_VaultCursorSource : uint8
{
    None    UMETA(DisplayName = "None"),
    Party   UMETA(DisplayName = "Party"),
    Vault     UMETA(DisplayName = "Vault")
};

/**
 * Cursor state struct
 */
USTRUCT(BlueprintType)
struct FGF_VaultCursorState
{
    GENERATED_BODY()

    // Is cursor holding a Creature?
    UPROPERTY(BlueprintReadOnly)
    bool bIsHolding = false;

    // Where did we grab from?
    UPROPERTY(BlueprintReadOnly)
    EGF_VaultCursorSource SourceLocation = EGF_VaultCursorSource::None;

    // Which index did we grab from?
    UPROPERTY(BlueprintReadOnly)
    int32 SourceIndex = -1;

    // The Creature being held
    UPROPERTY(BlueprintReadOnly)
    FGF_CreatureInstanceData HeldCreature;
};

/**
 * PC Controller - Handles PC Vault logic
 * Manages grab/drop operations, box switching, etc.
 */
UCLASS()
class GAMMAFRAMEWORKCREATURES_API UGF_VaultController : public UObject
{
    GENERATED_BODY()

public:
    //====================================================================================
    // INITIALIZATION
    //====================================================================================

    /**
     * Initialize with Creature Manager reference
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|PC")
    void InitializeWithManager(UGF_CreatureManagerSubsystem* InCreatureManager);

    //====================================================================================
    // GRAB/DROP OPERATIONS
    //====================================================================================

    /**
     * Grab Creature from party slot
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|PC")
    bool GrabFromParty(int32 SlotIndex);

    /**
     * Grab Creature from box slot
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|PC")
    bool GrabFromVault(int32 SlotIndex);

    /**
     * Drop held Creature to party slot
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|PC")
    bool DropToParty(int32 SlotIndex);

    /**
     * Drop held Creature to box slot
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|PC")
    bool DropToVault(int32 SlotIndex);

    /**
     * Check if can drop to party (won't remove last healthy Creature)
     */
    UFUNCTION(BlueprintPure, Category = "Creature|PC")
    bool CanDropToParty(int32 SlotIndex) const;

    /**
     * Clear cursor (return Creature to source)
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|PC")
    void ClearCursor();

    //====================================================================================
    // CURSOR STATE
    //====================================================================================

    /**
     * Is cursor holding a Creature?
     */
    UFUNCTION(BlueprintPure, Category = "Creature|PC")
    bool IsHoldingCreature() const { return CursorState.bIsHolding; }

    /**
     * Get held Creature data
     */
    UFUNCTION(BlueprintPure, Category = "Creature|PC")
    FGF_CreatureInstanceData GetHeldCreature() const { return CursorState.HeldCreature; }

    /**
     * Get cursor state
     */
    UFUNCTION(BlueprintPure, Category = "Creature|PC")
    FGF_VaultCursorState GetCursorState() const { return CursorState; }

    //====================================================================================
    // BOX MANAGEMENT
    //====================================================================================

    /**
     * Get current box index
     */
    UFUNCTION(BlueprintPure, Category = "Creature|PC")
    int32 GetCurrentVaultPageIndex() const { return CurrentVaultPageIndex; }

    /**
     * Get current box name
     */
    UFUNCTION(BlueprintPure, Category = "Creature|PC")
    FString GetCurrentVaultPageName() const;

    /**
     * Switch to next box
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|PC")
    void NextVaultPage();

    /**
     * Switch to previous box
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|PC")
    void PreviousVaultPage();

    /**
     * Set current box index
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|PC")
    void SetCurrentVaultPage(int32 VaultPageIndex);

    //====================================================================================
    // DATA ACCESS
    //====================================================================================

    /**
     * Get Creature data from party
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|PC")
    bool GetPartyCreatureData(int32 SlotIndex, FGF_CreatureInstanceData& OutData) const;

    /**
     * Get Creature data from current box
     */
    UFUNCTION(BlueprintCallable, Category = "Creature|PC")
    bool GetVaultCreatureData(int32 SlotIndex, FGF_CreatureInstanceData& OutData) const;

    //====================================================================================
    // EVENTS
    //====================================================================================

    /**
     * Called when Creature is grabbed
     */
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnCreatureGrabbed, FGF_VaultCursorState, CursorState);
    UPROPERTY(BlueprintAssignable, Category = "Creature|PC")
    FGF_OnCreatureGrabbed OnCreatureGrabbed;

    /**
     * Called when Creature is dropped
     */
    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGF_OnCreatureDropped);
    UPROPERTY(BlueprintAssignable, Category = "Creature|PC")
    FGF_OnCreatureDropped OnCreatureDropped;

    /**
     * Called when box is changed
     */
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnBoxChanged, int32, NewVaultPageIndex);
    UPROPERTY(BlueprintAssignable, Category = "Creature|PC")
    FGF_OnBoxChanged OnVaultChanged;

protected:
    // Creature Manager reference
    UPROPERTY()
    UGF_CreatureManagerSubsystem* CreatureManager = nullptr;

    // Current cursor state
    UPROPERTY()
    FGF_VaultCursorState CursorState;

    // Current box index
    UPROPERTY()
    int32 CurrentVaultPageIndex = 0;

private:
    // Helper: Check if would remove last healthy Creature from party
    bool WouldRemoveLastHealthyCreature(int32 PartySlotIndex) const;

    // Helper: Swap Creature between two locations
    void SwapCreature(EGF_VaultCursorSource SourceLocation, int32 SourceIndex,
                     EGF_VaultCursorSource TargetLocation, int32 TargetIndex);
};