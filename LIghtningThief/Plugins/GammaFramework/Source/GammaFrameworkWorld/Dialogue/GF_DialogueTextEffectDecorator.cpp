#include "GF_DialogueTextEffectDecorator.h"
#include "Components/RichTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Engine/DataTable.h"

// ============================================================================
// Glitch character pool
// ============================================================================

static const TCHAR GF_GlitchPool[] = TEXT("!@#$%&*?~<>[]{}|/\\0123456789");
static const int32 GF_GlitchPoolLen = UE_ARRAY_COUNT(GF_GlitchPool) - 1;

// ============================================================================
// Internal effect enum
// ============================================================================

enum class EGF_AnimatedEffect : uint8
{
	Shake,
	Tremble,
	Wave,
	Glitch,
	Pulse
};

// ============================================================================
// Settings bundle passed from UObject decorator to Slate widget
// ============================================================================

struct FGF_AnimatedTextSettings
{
	EGF_AnimatedEffect Effect = EGF_AnimatedEffect::Shake;
	float Param1 = 0.0f;  // Intensity / Amplitude / PulseSpeed
	float Param2 = 0.0f;  // ShakeInterval / WaveSpeed / GlitchChance / PulseMinOpacity
	float Param3 = 0.0f;  // WaveCharOffset
};

// ============================================================================
// SGF_DialogueAnimatedText — per-character animated Slate widget
// ============================================================================

class SGF_DialogueAnimatedText : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGF_DialogueAnimatedText) {}
		SLATE_ARGUMENT(FString, Text)
		SLATE_ARGUMENT(FGF_AnimatedTextSettings, Settings)
		SLATE_ARGUMENT(FSlateFontInfo, Font)
		SLATE_ARGUMENT(FSlateColor, ColorAndOpacity)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		EffectSettings  = InArgs._Settings;
		TimeAccumulator = 0.0f;
		ShakeTimer      = 0.0f;

		TSharedRef<SHorizontalBox> HBox = SNew(SHorizontalBox);

		const FString& SourceText = InArgs._Text;
		for (int32 i = 0; i < SourceText.Len(); ++i)
		{
			TSharedPtr<STextBlock> CharWidget;

			HBox->AddSlot()
			.AutoWidth()
			[
				SAssignNew(CharWidget, STextBlock)
				.Text(FText::FromString(FString(1, &SourceText[i])))
				.Font(InArgs._Font)
				.ColorAndOpacity(InArgs._ColorAndOpacity)
			];

			FGF_AnimChar Entry;
			Entry.Widget       = CharWidget;
			Entry.OriginalChar = SourceText[i];
			Entry.Index        = i;
			Entry.bIsGlitched  = false;
			Chars.Add(Entry);
		}

		ChildSlot[ HBox ];

		RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
			[this](double /*InCurrentTime*/, float InDeltaTime) -> EActiveTimerReturnType
			{
				UpdateAnimation(InDeltaTime);
				return EActiveTimerReturnType::Continue;
			}
		));
	}

private:
	struct FGF_AnimChar
	{
		TSharedPtr<STextBlock> Widget;
		TCHAR OriginalChar = 0;
		int32 Index        = 0;
		bool  bIsGlitched  = false;
	};

	void UpdateAnimation(float DeltaTime)
	{
		TimeAccumulator += DeltaTime;

		switch (EffectSettings.Effect)
		{
		case EGF_AnimatedEffect::Shake:   TickShake(DeltaTime); break;
		case EGF_AnimatedEffect::Tremble: TickTremble();        break;
		case EGF_AnimatedEffect::Wave:    TickWave();           break;
		case EGF_AnimatedEffect::Glitch:  TickGlitch();         break;
		case EGF_AnimatedEffect::Pulse:   TickPulse();          break;
		}
	}

	void TickShake(float DeltaTime)
	{
		ShakeTimer += DeltaTime;
		if (ShakeTimer < EffectSettings.Param2) return;
		ShakeTimer = 0.0f;

		const float Intensity = EffectSettings.Param1;
		for (FGF_AnimChar& C : Chars)
		{
			if (!C.Widget.IsValid()) continue;
			FVector2D Off(FMath::RandRange(-Intensity, Intensity),
			              FMath::RandRange(-Intensity, Intensity));
			C.Widget->SetRenderTransform(FSlateRenderTransform(1.0f, Off));
		}
	}

	void TickTremble()
	{
		const float Intensity = EffectSettings.Param1;
		for (FGF_AnimChar& C : Chars)
		{
			if (!C.Widget.IsValid()) continue;
			FVector2D Off(FMath::RandRange(-Intensity, Intensity),
			              FMath::RandRange(-Intensity, Intensity));
			C.Widget->SetRenderTransform(FSlateRenderTransform(1.0f, Off));
		}
	}

	void TickWave()
	{
		const float Amplitude = EffectSettings.Param1;
		const float Speed     = EffectSettings.Param2;
		const float CharOff   = EffectSettings.Param3;
		for (FGF_AnimChar& C : Chars)
		{
			if (!C.Widget.IsValid()) continue;
			float Y = FMath::Sin(TimeAccumulator * Speed + C.Index * CharOff) * Amplitude;
			C.Widget->SetRenderTransform(FSlateRenderTransform(1.0f, FVector2D(0.0f, Y)));
		}
	}

	void TickGlitch()
	{
		const float Intensity = EffectSettings.Param1;
		const float Chance    = EffectSettings.Param2;
		for (FGF_AnimChar& C : Chars)
		{
			if (!C.Widget.IsValid()) continue;

			if (FMath::FRand() < Chance)
			{
				int32 Idx = FMath::RandRange(0, GF_GlitchPoolLen - 1);
				C.Widget->SetText(FText::FromString(FString(1, &GF_GlitchPool[Idx])));
				FVector2D Off(FMath::RandRange(-Intensity, Intensity),
				              FMath::RandRange(-Intensity, Intensity));
				C.Widget->SetRenderTransform(FSlateRenderTransform(1.0f, Off));
				C.bIsGlitched = true;
			}
			else if (C.bIsGlitched)
			{
				C.Widget->SetText(FText::FromString(FString(1, &C.OriginalChar)));
				C.Widget->SetRenderTransform(FSlateRenderTransform(1.0f, FVector2D::ZeroVector));
				C.bIsGlitched = false;
			}
		}
	}

	void TickPulse()
	{
		const float Speed      = EffectSettings.Param1;
		const float MinOpacity = EffectSettings.Param2;
		float T = (FMath::Sin(TimeAccumulator * Speed * UE_TWO_PI) + 1.0f) * 0.5f;
		float Opacity = FMath::Lerp(MinOpacity, 1.0f, T);

		for (FGF_AnimChar& C : Chars)
		{
			if (!C.Widget.IsValid()) continue;
			C.Widget->SetRenderOpacity(Opacity);
		}
	}

	TArray<FGF_AnimChar>       Chars;
	FGF_AnimatedTextSettings EffectSettings;
	float                    TimeAccumulator;
	float                    ShakeTimer;
};

// ============================================================================
// FGF_DialogueEffectTextDecorator — bridges URichTextBlock to animated widget
// ============================================================================

class FGF_DialogueEffectTextDecorator : public FRichTextDecorator
{
public:
	FGF_DialogueEffectTextDecorator(URichTextBlock* InOwner, UGF_DialogueTextEffectDecorator* InDecorator)
		: FRichTextDecorator(InOwner)
		, Decorator(InDecorator)
	{}

	virtual bool Supports(const FTextRunParseResults& RunParseResult, const FString& Text) const override
	{
		return IsEffectTag(RunParseResult.Name);
	}

protected:
	virtual TSharedPtr<SWidget> CreateDecoratorWidget(
		const FTextRunInfo& RunInfo,
		const FTextBlockStyle& DefaultStyle) const override
	{
		FString BaseName;
		bool    bBold = false, bItalic = false;
		FString ColorName;
		ParseTagName(RunInfo.Name, BaseName, bBold, bItalic, ColorName);

		FGF_AnimatedTextSettings Settings;
		ResolveSettings(BaseName, Settings);

		FSlateFontInfo Font  = DefaultStyle.Font;
		FSlateColor    Color = DefaultStyle.ColorAndOpacity;

		// Resolve named style (e.g. "Big", "Orange") from the DataTable.
		// This applies BOTH the font (size, family) AND color from the row —
		// so <Shake.Big> correctly uses the Big row's font size, not just its color.
		if (!ColorName.IsEmpty())
		{
			ResolveStyle(ColorName, DefaultStyle, Font, Color);
		}

		// Apply Bold/Italic AFTER the DataTable lookup so they always win and
		// can combine with named styles: <Shake.Big.Bold> = Big size + Bold typeface.
		if (bBold && bItalic)   Font.TypefaceFontName = FName("BoldItalic");
		else if (bBold)         Font.TypefaceFontName = FName("Bold");
		else if (bItalic)       Font.TypefaceFontName = FName("Italic");

		return SNew(SGF_DialogueAnimatedText)
			.Text(RunInfo.Content.ToString())
			.Settings(Settings)
			.Font(Font)
			.ColorAndOpacity(Color);
	}

private:
	bool IsEffectTag(const FString& TagName) const
	{
		FString BaseName = TagName;
		int32 DotIdx;
		if (TagName.FindChar(TEXT('.'), DotIdx))
		{
			BaseName = TagName.Left(DotIdx);
		}

		static const TArray<FString> EffectNames = {
			TEXT("Shake"), TEXT("Tremble"), TEXT("Wave"), TEXT("Glitch"), TEXT("Pulse")
		};

		for (const FString& Name : EffectNames)
		{
			if (BaseName.Equals(Name, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	void ParseTagName(const FString& TagName, FString& OutBaseName, bool& bOutBold, bool& bOutItalic, FString& OutColorName) const
	{
		bOutBold   = false;
		bOutItalic = false;
		OutColorName.Empty();

		TArray<FString> Parts;
		TagName.ParseIntoArray(Parts, TEXT("."));

		if (Parts.Num() == 0)
		{
			OutBaseName = TagName;
			return;
		}

		OutBaseName = Parts[0];

		for (int32 i = 1; i < Parts.Num(); ++i)
		{
			if (Parts[i].Equals(TEXT("Bold"), ESearchCase::IgnoreCase))
			{
				bOutBold = true;
			}
			else if (Parts[i].Equals(TEXT("Italic"), ESearchCase::IgnoreCase))
			{
				bOutItalic = true;
			}
			else if (Parts[i].Equals(TEXT("BoldItalic"), ESearchCase::IgnoreCase))
			{
				bOutBold = bOutItalic = true;
			}
			else
			{
				OutColorName = Parts[i];
			}
		}
	}

	// Looks up a named style row (e.g. "Big", "Orange") and applies its font AND
	// color to InOutFont / OutColor.  Font size, font object, and color are all
	// taken from the row so compound tags like <Shake.Big> use the correct size.
	//
	// Style table priority:
	//   1. Decorator->DialogueStyleSet  (explicit override — optional)
	//   2. Owner->TextStyleSet          (the RichTextBlock's own table — automatic fallback)
	// This means DialogueStyleSet never NEEDS to be set; the decorator finds the
	// right table automatically, so nothing resets when the engine restarts.
	void ResolveStyle(const FString& StyleName, const FTextBlockStyle& DefaultStyle,
	                  FSlateFontInfo& InOutFont, FSlateColor& OutColor) const
	{
		UDataTable* StyleSet = Decorator->DialogueStyleSet;
		if (!StyleSet && Owner)
		{
			StyleSet = Owner->GetTextStyleSet();
		}
		if (!StyleSet) return;

		FRichTextStyleRow* Row = StyleSet->FindRow<FRichTextStyleRow>(FName(*StyleName), TEXT(""));
		if (!Row) return;

		const FSlateFontInfo& RowFont = Row->TextStyle.Font;

		// Apply font family / asset if the row specifies one
		if (RowFont.FontObject != nullptr)
		{
			InOutFont.FontObject = RowFont.FontObject;
		}
		// Apply font size from the row
		InOutFont.Size = RowFont.Size;

		// Apply color from the row
		OutColor = Row->TextStyle.ColorAndOpacity;
	}

	void ResolveSettings(const FString& BaseName, FGF_AnimatedTextSettings& OutSettings) const
	{
		if (BaseName.Equals(TEXT("Shake"), ESearchCase::IgnoreCase))
		{
			OutSettings.Effect = EGF_AnimatedEffect::Shake;
			OutSettings.Param1 = Decorator->ShakeIntensity;
			OutSettings.Param2 = Decorator->ShakeInterval;
		}
		else if (BaseName.Equals(TEXT("Tremble"), ESearchCase::IgnoreCase))
		{
			OutSettings.Effect = EGF_AnimatedEffect::Tremble;
			OutSettings.Param1 = Decorator->TrembleIntensity;
		}
		else if (BaseName.Equals(TEXT("Wave"), ESearchCase::IgnoreCase))
		{
			OutSettings.Effect = EGF_AnimatedEffect::Wave;
			OutSettings.Param1 = Decorator->WaveAmplitude;
			OutSettings.Param2 = Decorator->WaveSpeed;
			OutSettings.Param3 = Decorator->WaveCharOffset;
		}
		else if (BaseName.Equals(TEXT("Glitch"), ESearchCase::IgnoreCase))
		{
			OutSettings.Effect = EGF_AnimatedEffect::Glitch;
			OutSettings.Param1 = Decorator->GlitchIntensity;
			OutSettings.Param2 = Decorator->GlitchChance;
		}
		else if (BaseName.Equals(TEXT("Pulse"), ESearchCase::IgnoreCase))
		{
			OutSettings.Effect = EGF_AnimatedEffect::Pulse;
			OutSettings.Param1 = Decorator->PulseSpeed;
			OutSettings.Param2 = Decorator->PulseMinOpacity;
		}
	}

	UGF_DialogueTextEffectDecorator* Decorator;
};

// ============================================================================
// UGF_DialogueTextEffectDecorator
// ============================================================================

UGF_DialogueTextEffectDecorator::UGF_DialogueTextEffectDecorator(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

TSharedPtr<ITextDecorator> UGF_DialogueTextEffectDecorator::CreateDecorator(URichTextBlock* InOwner)
{
	return MakeShareable(new FGF_DialogueEffectTextDecorator(InOwner, this));
}
