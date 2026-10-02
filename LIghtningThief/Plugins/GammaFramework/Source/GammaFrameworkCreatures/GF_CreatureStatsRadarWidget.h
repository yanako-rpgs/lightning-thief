// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GF_CreatureStatsRadarWidget.generated.h"

/**
 * Creature Stats Radar Chart Widget
 * Displays a hexagonal radar chart for Creature stats (HP, ATK, DEF, SP.ATK, SP.DEF, SPD)
 * Supports real-time animation and dual display (current stats vs APs)
 */
UCLASS()
class GAMMAFRAMEWORKCREATURES_API UGF_CreatureStatsRadarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UGF_CreatureStatsRadarWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeConstruct() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	// Set the Creature stats to display
	UFUNCTION(BlueprintCallable, Category = "Creature Stats")
	void SetStats(int32 HP, int32 Attack, int32 Defense, int32 Magic, int32 Poise, int32 Speed);

	// Set the Creature APs to display
	UFUNCTION(BlueprintCallable, Category = "Creature Stats")
	void SetAPs(int32 HP_AP, int32 Attack_AP, int32 Defense_AP, int32 Magic_AP, int32 Poise_AP, int32 Speed_AP);

	// Animate the chart outward (0.0 to 1.0)
	UFUNCTION(BlueprintCallable, Category = "Creature Stats")
	void SetAnimationProgress(float Progress);

	// Reset animation to 0
	UFUNCTION(BlueprintCallable, Category = "Creature Stats")
	void ResetAnimation();

protected:
	// Current stats (0-255)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	int32 Stat_HP = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	int32 Stat_Attack = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	int32 Stat_Defense = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	int32 Stat_Magic = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	int32 Stat_Poise = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	int32 Stat_Speed = 100;

	// APs (0-50)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "APs")
	int32 AP_HP = 50;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "APs")
	int32 AP_Attack = 50;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "APs")
	int32 AP_Defense = 50;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "APs")
	int32 AP_Magic = 50;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "APs")
	int32 AP_Poise = 50;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "APs")
	int32 AP_Speed = 50;

	// Display settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	float ChartRadius = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	float LabelDistance = 190.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	float MaxStatValue = 255.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	float MaxAPValue = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	bool bShowLabels = true;

	// Label text per chart point, clockwise from the top:
	// 0=top, 1=top-right, 2=bottom-right, 3=bottom, 4=bottom-left, 5=top-left
	UPROPERTY(EditAnywhere, BlueprintReadWrite, EditFixedSize, Category = "Display")
	TArray<FText> StatLabels;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	bool bShowValues = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	bool bShowAPs = true;

	// Animation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	float AnimationProgress = 1.0f;

	// Colors
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor GridColor = FLinearColor(0.2f, 0.4f, 0.2f, 0.5f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor StatFillColor = FLinearColor(0.5f, 0.76f, 0.29f, 0.3f); // Green

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor StatLineColor = FLinearColor(0.5f, 0.76f, 0.29f, 1.0f); // Green

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor APFillColor = FLinearColor(1.0f, 0.65f, 0.15f, 0.3f); // Orange transparent

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor APLineColor = FLinearColor(1.0f, 0.65f, 0.15f, 1.0f); // Orange

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor LabelColor = FLinearColor(0.5f, 0.76f, 0.29f, 1.0f); // Green

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor ValueColor = FLinearColor(1.0f, 0.65f, 0.15f, 1.0f); // Orange

	// Font
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Font")
	FSlateFontInfo LabelFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Font")
	FSlateFontInfo ValueFont;

private:
	// Helper functions for drawing
	FVector2D GetStatPoint(int32 StatIndex, float Value, float MaxValue, const FVector2D& Center) const;
	void DrawHexagonGrid(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	void DrawStatPolygon(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, bool bIsAP) const;
	void DrawLabels(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

	// Get stat value by index
	float GetStatByIndex(int32 Index) const;
	float GetAPByIndex(int32 Index) const;
	FString GetStatLabel(int32 Index) const;
};