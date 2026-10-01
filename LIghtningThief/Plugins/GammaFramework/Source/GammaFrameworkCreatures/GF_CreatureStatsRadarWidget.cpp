// Fill out your copyright notice in the Description page of Project Settings.

#include "GF_CreatureStatsRadarWidget.h"
#include "Slate/SlateBrushAsset.h"
#include "Rendering/DrawElements.h"
// FSlateApplication / FSlateFontMeasure used by the ->Measure() calls below. These used to
// arrive transitively via CreatureSpeciesData.h -> CkAssetThumbnailRenderer.h; that include is
// gone, and the game target's shared PCH does not pull in Slate the way the editor's does.
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"

UGF_CreatureStatsRadarWidget::UGF_CreatureStatsRadarWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Initialize fonts with default values
	// You can customize these in the widget's Details panel in editor

	StatLabels = {
		NSLOCTEXT("GF_CreatureStatsRadar", "LabelHP", "HP"),
		NSLOCTEXT("GF_CreatureStatsRadar", "LabelATK", "ATK"),
		NSLOCTEXT("GF_CreatureStatsRadar", "LabelDEF", "DEF"),
		NSLOCTEXT("GF_CreatureStatsRadar", "LabelSPD", "SPD"),
		NSLOCTEXT("GF_CreatureStatsRadar", "LabelSpDEF", "Sp. DEF"),
		NSLOCTEXT("GF_CreatureStatsRadar", "LabelSpATK", "Sp. ATK")
	};
}

void UGF_CreatureStatsRadarWidget::NativeConstruct()
{
	Super::NativeConstruct();
}

int32 UGF_CreatureStatsRadarWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Call parent
	int32 MaxLayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	// Draw grid
	DrawHexagonGrid(AllottedGeometry, OutDrawElements, MaxLayerId);

	// Draw Potential polygon (orange outline - behind the stat polygon)
	if (bShowPotentials)
	{
		DrawStatPolygon(AllottedGeometry, OutDrawElements, MaxLayerId + 1, true);
	}

	// Draw stat polygon (green fill + outline)
	DrawStatPolygon(AllottedGeometry, OutDrawElements, MaxLayerId + 2, false);

	// Draw labels and values
	if (bShowLabels)
	{
		DrawLabels(AllottedGeometry, OutDrawElements, MaxLayerId + 3);
	}

	return MaxLayerId + 4;
}

void UGF_CreatureStatsRadarWidget::SetStats(int32 HP, int32 Attack, int32 Defense, int32 Magic, int32 Poise, int32 Speed)
{
	Stat_HP = HP;
	Stat_Attack = Attack;
	Stat_Defense = Defense;
	Stat_Magic = Magic;
	Stat_Poise = Poise;
	Stat_Speed = Speed;
}

void UGF_CreatureStatsRadarWidget::SetPotentials(int32 HP_Potential, int32 Attack_Potential, int32 Defense_Potential, int32 Magic_Potential, int32 Poise_Potential, int32 Speed_Potential)
{
	Potential_HP = HP_Potential;
	Potential_Attack = Attack_Potential;
	Potential_Defense = Defense_Potential;
	IV_Magic = Magic_Potential;
	IV_Poise = Poise_Potential;
	Potential_Speed = Speed_Potential;
}

void UGF_CreatureStatsRadarWidget::SetAnimationProgress(float Progress)
{
	AnimationProgress = FMath::Clamp(Progress, 0.0f, 1.0f);
}

void UGF_CreatureStatsRadarWidget::ResetAnimation()
{
	AnimationProgress = 0.0f;
}

FVector2D UGF_CreatureStatsRadarWidget::GetStatPoint(int32 StatIndex, float Value, float MaxValue, const FVector2D& Center) const
{
	// Calculate angle for hexagon (start from top, go clockwise)
	// Stat order: 0=HP(top), 1=ATK(top-right), 2=DEF(bottom-right), 3=SP.DEF(bottom), 4=SP.ATK(bottom-left), 5=SPD(top-left)
	const float AngleOffset = -90.0f; // Start from top
	const float AngleDegrees = AngleOffset + (360.0f / 6.0f) * StatIndex;
	const float AngleRadians = FMath::DegreesToRadians(AngleDegrees);

	// Calculate the scale based on value and animation
	float Scale = (Value / MaxValue) * AnimationProgress;
	Scale = FMath::Clamp(Scale, 0.0f, 1.0f);

	// Calculate point position
	float Distance = ChartRadius * Scale;
	FVector2D Point;
	Point.X = Center.X + Distance * FMath::Cos(AngleRadians);
	Point.Y = Center.Y + Distance * FMath::Sin(AngleRadians);

	return Point;
}

void UGF_CreatureStatsRadarWidget::DrawHexagonGrid(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	FVector2D Center = LocalSize * 0.5f;

	// Draw 5 concentric hexagons (20%, 40%, 60%, 80%, 100%)
	for (int32 i = 1; i <= 5; i++)
	{
		float Scale = i * 0.2f;
		TArray<FVector2D> HexPoints;

		// Generate hexagon points
		for (int32 j = 0; j < 6; j++)
		{
			const float AngleOffset = -90.0f;
			const float AngleDegrees = AngleOffset + (360.0f / 6.0f) * j;
			const float AngleRadians = FMath::DegreesToRadians(AngleDegrees);

			FVector2D Point;
			Point.X = Center.X + ChartRadius * Scale * FMath::Cos(AngleRadians);
			Point.Y = Center.Y + ChartRadius * Scale * FMath::Sin(AngleRadians);
			HexPoints.Add(Point);
		}

		// Draw hexagon lines
		for (int32 j = 0; j < 6; j++)
		{
			FVector2D Start = HexPoints[j];
			FVector2D End = HexPoints[(j + 1) % 6];

			TArray<FVector2D> LinePoints;
			LinePoints.Add(Start);
			LinePoints.Add(End);

			float Thickness = (i == 5) ? 2.0f : 1.0f;
			FLinearColor Color = GridColor;
			if (i == 5)
			{
				Color.A = 0.5f;
			}
			else
			{
				Color.A = 0.3f;
			}

			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(),
				LinePoints,
				ESlateDrawEffect::None,
				Color,
				true,
				Thickness
			);
		}
	}

	// Draw radial lines from center to vertices
	for (int32 i = 0; i < 6; i++)
	{
		const float AngleOffset = -90.0f;
		const float AngleDegrees = AngleOffset + (360.0f / 6.0f) * i;
		const float AngleRadians = FMath::DegreesToRadians(AngleDegrees);

		FVector2D End;
		End.X = Center.X + ChartRadius * FMath::Cos(AngleRadians);
		End.Y = Center.Y + ChartRadius * FMath::Sin(AngleRadians);

		TArray<FVector2D> LinePoints;
		LinePoints.Add(Center);
		LinePoints.Add(End);

		FLinearColor Color = GridColor;
		Color.A = 0.3f;

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(),
			LinePoints,
			ESlateDrawEffect::None,
			Color,
			true,
			1.0f
		);
	}
}

void UGF_CreatureStatsRadarWidget::DrawStatPolygon(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, bool bIsPotential) const
{
	FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	FVector2D Center = LocalSize * 0.5f;

	TArray<FVector2D> Points;

	// Generate polygon points for all 6 stats
	for (int32 i = 0; i < 6; i++)
	{
		float Value = bIsPotential ? GetPotentialByIndex(i) : GetStatByIndex(i);
		float MaxValue = bIsPotential ? MaxPotentialValue : MaxStatValue;
		FVector2D Point = GetStatPoint(i, Value, MaxValue, Center);
		Points.Add(Point);
	}

	if (Points.Num() < 3)
	{
		return; // Can't draw polygon with less than 3 points
	}

	// Draw outline
	FLinearColor LineColor = bIsPotential ? PotentialLineColor : StatLineColor;
	float Thickness = bIsPotential ? 2.0f : 2.5f;

	for (int32 i = 0; i < 6; i++)
	{
		TArray<FVector2D> LinePoints;
		LinePoints.Add(Points[i]);
		LinePoints.Add(Points[(i + 1) % 6]);

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(),
			LinePoints,
			ESlateDrawEffect::None,
			LineColor,
			true,
			Thickness
		);
	}

	// Draw filled polygon for both stats and Potentials
	// Note: Due to UMG rendering limitations, we'll use a scanline approach
	if (AnimationProgress > 0.01f)
	{
		// Determine fill color based on whether this is Potential or Stat
		FLinearColor FillColor = bIsPotential ? PotentialFillColor : StatFillColor;
		const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("WhiteBrush");

		// For each triangle, fill it by drawing horizontal scanlines
		for (int32 i = 0; i < 6; i++)
		{
			FVector2D Point1 = Points[i];
			FVector2D Point2 = Points[(i + 1) % 6];

			// Find the bounding box of the triangle
			float MinY = FMath::Min3(Center.Y, Point1.Y, Point2.Y);
			float MaxY = FMath::Max3(Center.Y, Point1.Y, Point2.Y);

			// Draw horizontal scanlines
			const float ScanlineStep = 1.0f; // 1 pixel spacing
			for (float Y = MinY; Y <= MaxY; Y += ScanlineStep)
			{
				// Find intersection points with triangle edges at this Y
				TArray<float> IntersectX;

				// Check each edge
				auto CheckEdge = [&](FVector2D A, FVector2D B)
				{
					if ((A.Y <= Y && B.Y >= Y) || (A.Y >= Y && B.Y <= Y))
					{
						if (FMath::Abs(B.Y - A.Y) > 0.01f)
						{
							float T = (Y - A.Y) / (B.Y - A.Y);
							float X = FMath::Lerp(A.X, B.X, T);
							IntersectX.Add(X);
						}
					}
				};

				CheckEdge(Center, Point1);
				CheckEdge(Point1, Point2);
				CheckEdge(Point2, Center);

				// Draw line between intersection points
				if (IntersectX.Num() >= 2)
				{
					IntersectX.Sort();
					float StartX = IntersectX[0];
					float EndX = IntersectX[IntersectX.Num() - 1];

					if (EndX - StartX > 0.5f)
					{
						FSlateDrawElement::MakeBox(
							OutDrawElements,
							LayerId - 1,
							AllottedGeometry.ToPaintGeometry(
								FVector2D(StartX, Y),
								FVector2D(EndX - StartX, ScanlineStep)
							),
							WhiteBrush,
							ESlateDrawEffect::None,
							FillColor
						);
					}
				}
			}
		}
	}

	// Draw points/vertices
	for (const FVector2D& Point : Points)
	{
		// Draw a small circle at each vertex
		TArray<FVector2D> CirclePoints;
		const int32 CircleSegments = 8;
		const float CircleRadius = bIsPotential ? 3.0f : 4.0f;

		for (int32 i = 0; i <= CircleSegments; i++)
		{
			float Angle = (2.0f * PI * i) / CircleSegments;
			FVector2D CirclePoint;
			CirclePoint.X = Point.X + CircleRadius * FMath::Cos(Angle);
			CirclePoint.Y = Point.Y + CircleRadius * FMath::Sin(Angle);
			CirclePoints.Add(CirclePoint);
		}

		if (CirclePoints.Num() > 1)
		{
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId + 1,
				AllottedGeometry.ToPaintGeometry(),
				CirclePoints,
				ESlateDrawEffect::None,
				LineColor,
				true,
				Thickness
			);
		}
	}
}

void UGF_CreatureStatsRadarWidget::DrawLabels(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	FVector2D Center = LocalSize * 0.5f;

	// Use a default font if not set
	FSlateFontInfo UseLabelFont = LabelFont.HasValidFont() ? LabelFont : FCoreStyle::GetDefaultFontStyle("Bold", 14);
	FSlateFontInfo UseValueFont = ValueFont.HasValidFont() ? ValueFont : FCoreStyle::GetDefaultFontStyle("Bold", 12);

	for (int32 i = 0; i < 6; i++)
	{
		// Calculate label position (further out than the chart)
		const float AngleOffset = -90.0f;
		const float AngleDegrees = AngleOffset + (360.0f / 6.0f) * i;
		const float AngleRadians = FMath::DegreesToRadians(AngleDegrees);

		FVector2D LabelPos;
		LabelPos.X = Center.X + LabelDistance * FMath::Cos(AngleRadians);
		LabelPos.Y = Center.Y + LabelDistance * FMath::Sin(AngleRadians);

		// Get stat info
		FString StatLabel = GetStatLabel(i);
		float StatValue = GetStatByIndex(i);
		float PotentialValue = GetPotentialByIndex(i);

		// Measure text size for proper positioning
		FVector2D LabelSize = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(StatLabel, UseLabelFont);

		// Adjust text position based on location on hexagon
		FVector2D AdjustedLabelPos = LabelPos;
		FVector2D ValueOffset(0.0f, 18.0f);

		if (i == 0) // Top (HP)
		{
			AdjustedLabelPos.X -= LabelSize.X * 0.5f; // Center horizontally
			AdjustedLabelPos.Y -= LabelSize.Y + 5.0f; // Above point
			ValueOffset = FVector2D(0.0f, LabelSize.Y + 3.0f);
		}
		else if (i == 3) // Bottom (SPD)
		{
			AdjustedLabelPos.X -= LabelSize.X * 0.5f; // Center horizontally
			AdjustedLabelPos.Y += 5.0f; // Below point
			ValueOffset = FVector2D(0.0f, LabelSize.Y + 5.0f); // Fixed: reduced from 18.0f
		}
		else if (i == 1 || i == 2) // Right side (ATK, DEF)
		{
			AdjustedLabelPos.X += 5.0f; // To the right
			AdjustedLabelPos.Y -= LabelSize.Y * 0.5f; // Center vertically
			ValueOffset = FVector2D(0.0f, LabelSize.Y + 3.0f); // Below label, small gap
		}
		else // Left side (Sp. DEF, Sp. ATK)
		{
			AdjustedLabelPos.X -= LabelSize.X + 5.0f; // To the left
			AdjustedLabelPos.Y -= LabelSize.Y * 0.5f; // Center vertically
			ValueOffset = FVector2D(0.0f, LabelSize.Y + 3.0f); // Below label, small gap (was 18.0f)
		}

		// Draw stat label
		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(AdjustedLabelPos, FVector2D(1.0f, 1.0f)),
			StatLabel,
			UseLabelFont,
			ESlateDrawEffect::None,
			LabelColor
		);

		// Draw stat value
		if (bShowValues)
		{
			if (bShowPotentials)
			{
				// Draw in three parts: [GREEN stat] [WHITE /] [ORANGE Potential]
				FString StatText = FString::Printf(TEXT("%.0f"), StatValue);
				FString SlashText = TEXT("/");
				FString PotentialText = FString::Printf(TEXT("%.0f"), PotentialValue);

				// Measure each part
				FVector2D StatSize = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(StatText, UseValueFont);
				FVector2D SlashSize = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(SlashText, UseValueFont);
				FVector2D PotentialSize = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(PotentialText, UseValueFont);

				FVector2D TotalSize = FVector2D(StatSize.X + SlashSize.X + PotentialSize.X, StatSize.Y);
				FVector2D ValuePos = AdjustedLabelPos + ValueOffset;

				// Center the entire value text
				if (i == 0 || i == 3) // Top or bottom
				{
					ValuePos.X -= TotalSize.X * 0.5f;
				}
				else if (i == 4 || i == 5) // Left side - center under label
				{
					ValuePos.X += (LabelSize.X - TotalSize.X) * 0.5f;
				}
				// Right side (i == 1 || i == 2) stays left-aligned

				// Draw stat value (GREEN)
				FSlateDrawElement::MakeText(
					OutDrawElements,
					LayerId,
					AllottedGeometry.ToPaintGeometry(ValuePos, FVector2D(1.0f, 1.0f)),
					StatText,
					UseValueFont,
					ESlateDrawEffect::None,
					StatLineColor // Green color for current stat
				);

				// Draw slash (WHITE or light color)
				FSlateDrawElement::MakeText(
					OutDrawElements,
					LayerId,
					AllottedGeometry.ToPaintGeometry(ValuePos + FVector2D(StatSize.X, 0.0f), FVector2D(1.0f, 1.0f)),
					SlashText,
					UseValueFont,
					ESlateDrawEffect::None,
					FLinearColor(0.9f, 0.9f, 0.9f, 1.0f) // Light gray/white
				);

				// Draw Potential value (ORANGE)
				FSlateDrawElement::MakeText(
					OutDrawElements,
					LayerId,
					AllottedGeometry.ToPaintGeometry(ValuePos + FVector2D(StatSize.X + SlashSize.X, 0.0f), FVector2D(1.0f, 1.0f)),
					PotentialText,
					UseValueFont,
					ESlateDrawEffect::None,
					PotentialLineColor // Orange color for Potential
				);
			}
			else
			{
				// Single stat value (no Potential shown)
				FString ValueText = FString::Printf(TEXT("%.0f"), StatValue);
				FVector2D ValueSize = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(ValueText, UseValueFont);
				FVector2D ValuePos = AdjustedLabelPos + ValueOffset;

				// Center the value text
				if (i == 0 || i == 3) // Top or bottom
				{
					ValuePos.X -= ValueSize.X * 0.5f;
				}
				else if (i == 4 || i == 5) // Left side - center under label
				{
					ValuePos.X += (LabelSize.X - ValueSize.X) * 0.5f;
				}
				// Right side (i == 1 || i == 2) stays left-aligned

				FSlateDrawElement::MakeText(
					OutDrawElements,
					LayerId,
					AllottedGeometry.ToPaintGeometry(ValuePos, FVector2D(1.0f, 1.0f)),
					ValueText,
					UseValueFont,
					ESlateDrawEffect::None,
					StatLineColor // Green for stat value
				);
			}
		}
	}
}

float UGF_CreatureStatsRadarWidget::GetStatByIndex(int32 Index) const
{
	switch (Index)
	{
	case 0: return Stat_HP;
	case 1: return Stat_Attack;
	case 2: return Stat_Defense;
	case 3: return Stat_Speed;      // Changed: SPD at bottom
	case 4: return Stat_Poise;      // Changed: Sp. DEF at bottom-left
	case 5: return Stat_Magic;      // Changed: Sp. ATK at top-left
	default: return 0.0f;
	}
}

float UGF_CreatureStatsRadarWidget::GetPotentialByIndex(int32 Index) const
{
	switch (Index)
	{
	case 0: return Potential_HP;
	case 1: return Potential_Attack;
	case 2: return Potential_Defense;
	case 3: return Potential_Speed;        // Changed: SPD at bottom
	case 4: return IV_Poise;        // Changed: Sp. DEF at bottom-left
	case 5: return IV_Magic;        // Changed: Sp. ATK at top-left
	default: return 0.0f;
	}
}

FString UGF_CreatureStatsRadarWidget::GetStatLabel(int32 Index) const
{
	return StatLabels.IsValidIndex(Index) ? StatLabels[Index].ToString() : FString();
}