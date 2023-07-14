// Copyright (c) Sam Bloomberg

#include "DialogueBox.h"
#include "Engine/Font.h"
#include "Styling/SlateStyle.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "TimerManager.h"

#include <Framework/Text/SlateTextRun.h>
#include <Framework/Text/RichTextMarkupProcessing.h>
#include <Framework/Text/ShapedTextCache.h>

class FDialogueBoxTextRun : public FSlateTextRun
{
public:
	FVector2D MeasureInternal( int32 BeginIndex, int32 EndIndex, float Scale, const FRunTextContext& TextContext, const FString& InText ) const
	{
		const FVector2D ShadowOffsetToApply((EndIndex == Range.EndIndex) ? FMath::Abs(Style.ShadowOffset.X * Scale) : 0.0f, FMath::Abs(Style.ShadowOffset.Y * Scale));

		// Offset the measured shaped text by the outline since the outline was not factored into the size of the text
		// Need to add the outline offsetting to the beginning and the end because it surrounds both sides.
		const float ScaledOutlineSize = Style.Font.OutlineSettings.OutlineSize * Scale;
		const FVector2D OutlineSizeToApply((BeginIndex == Range.BeginIndex ? ScaledOutlineSize : 0) + (EndIndex == Range.EndIndex ? ScaledOutlineSize : 0), ScaledOutlineSize);

		if (EndIndex - BeginIndex == 0)
		{
			return FVector2D(0, GetMaxHeight(Scale)) + ShadowOffsetToApply + OutlineSizeToApply;
		}

		// Use the full text range (rather than the run range) so that text that spans runs will still be shaped correctly
		return ShapedTextCacheUtil::MeasureShapedText(TextContext.ShapedTextCache, FCachedShapedTextKey(FTextRange(0, InText.Len()), Scale, TextContext, Style.Font), FTextRange(BeginIndex, EndIndex), *InText) + ShadowOffsetToApply + OutlineSizeToApply;
	}

	FVector2D Measure(int32 StartIndex, int32 EndIndex, float Scale, const FRunTextContext& TextContext) const override
	{
		if (EndIndex != Range.EndIndex)
		{
			return FSlateTextRun::Measure(StartIndex, EndIndex, Scale, TextContext);
		}

		int32 partialContent = Range.Len();
		check(partialContent >= 0);

		FString futureContent;
		if (!Segment.RunInfo.ContentRange.IsEmpty())
		{
			// with tags
			futureContent = Segment.Text.Mid(Segment.RunInfo.ContentRange.BeginIndex - Segment.RunInfo.OriginalRange.BeginIndex + partialContent, Segment.RunInfo.ContentRange.Len() - partialContent);
		}
		else
		{
			// no tags
			futureContent = Segment.Text.Mid(partialContent, Segment.RunInfo.OriginalRange.Len() - partialContent);
		}
		for (int32 i = 0; i < futureContent.Len(); ++i)
		{
			TCHAR futureChar = futureContent[i];
			if (FText::IsWhitespace(futureChar))
			{
				futureContent.LeftInline(i);
				break;
			}
		}

		FString combinedContent = *Text + futureContent;
		return MeasureInternal(StartIndex, combinedContent.Len(), Scale, TextContext, combinedContent);
	}

	FDialogueBoxTextRun(const FRunInfo& InRunInfo, const TSharedRef< const FString >& InText, const FTextBlockStyle& InStyle, const FDialogueTextSegment& Segment)
		:
		FSlateTextRun(InRunInfo, InText, InStyle),
		Segment(Segment)
	{
	}

	FDialogueBoxTextRun(const FRunInfo& InRunInfo, const TSharedRef< const FString >& InText, const FTextBlockStyle& InStyle, const FTextRange& InRange, const FDialogueTextSegment& Segment)
		:
		FSlateTextRun(InRunInfo, InText, InStyle, InRange),
		Segment(Segment)
	{
	}

private:
	const FDialogueTextSegment& Segment;
};

class FDialogueBoxTextDecorator : public ITextDecorator
{
public:
	FDialogueBoxTextDecorator(const TArray<FDialogueTextSegment>* Segments, const int32* CurrentSegmentIndex)
		:
		Segments(Segments),
		CurrentSegmentIndex(CurrentSegmentIndex)
	{
	}

	bool Supports(const FTextRunParseResults& RunInfo, const FString& Text) const override
	{
		// no segments have been calculated yet
		if (*CurrentSegmentIndex >= Segments->Num())
		{
			return false;
		}

		// does this run relate to the segment which is still in-flight?
		const FDialogueTextSegment& segment = (*Segments)[*CurrentSegmentIndex];
		const FTextRange& segmentRange = !RunInfo.ContentRange.IsEmpty() ? segment.RunInfo.ContentRange : segment.RunInfo.OriginalRange;
		const FTextRange& runRange = !RunInfo.ContentRange.IsEmpty() ? RunInfo.ContentRange : RunInfo.OriginalRange;
		auto intersected = runRange.Intersect(segmentRange);
		return !intersected.IsEmpty() && segmentRange != intersected;
	}

	TSharedRef<ISlateRun> Create(const TSharedRef<class FTextLayout>& TextLayout, const FTextRunParseResults& InRunInfo, const FString& ProcessedString, const TSharedRef<FString>& InOutModelText, const ISlateStyle* InStyle) override
	{
		FRunInfo RunInfo(InRunInfo.Name);
		for (const TPair<FString, FTextRange>& Pair : InRunInfo.MetaData)
		{
			int32 Length = FMath::Max(0, Pair.Value.EndIndex - Pair.Value.BeginIndex);
			RunInfo.MetaData.Add(Pair.Key, ProcessedString.Mid(Pair.Value.BeginIndex, Length));
		}

		const FTextBlockStyle* TextBlockStyle;
		FTextRange ModelRange;
		ModelRange.BeginIndex = InOutModelText->Len();
		if (!(InRunInfo.Name.IsEmpty()) && InStyle->HasWidgetStyle< FTextBlockStyle >(FName(*InRunInfo.Name)))
		{
			*InOutModelText += ProcessedString.Mid(InRunInfo.ContentRange.BeginIndex, InRunInfo.ContentRange.Len());
			TextBlockStyle = &(InStyle->GetWidgetStyle< FTextBlockStyle >(FName(*InRunInfo.Name)));
		}
		else
		{
			*InOutModelText += ProcessedString.Mid(InRunInfo.OriginalRange.BeginIndex, InRunInfo.OriginalRange.Len());
			TextBlockStyle = &static_cast<FSlateTextLayout&>(*TextLayout).GetDefaultTextStyle();
		}
		ModelRange.EndIndex = InOutModelText->Len();

		const FDialogueTextSegment& Segment = (*Segments)[*CurrentSegmentIndex];
		return MakeShared<FDialogueBoxTextRun>(RunInfo, InOutModelText, *TextBlockStyle, ModelRange, Segment);
	}

private:
	const TArray<FDialogueTextSegment>* Segments;
	const int32* CurrentSegmentIndex;
};



TSharedRef<SWidget> UDialogueTextBlock::RebuildWidget()
{
	// Copied from URichTextBlock::RebuildWidget
	UpdateStyleData();

	TArray< TSharedRef< class ITextDecorator > > CreatedDecorators;
	CreateDecorators(CreatedDecorators);

	TextParser = CreateMarkupParser();
	TSharedRef<FRichTextLayoutMarshaller> Marshaller = FRichTextLayoutMarshaller::Create(TextParser, CreateMarkupWriter(), CreatedDecorators, StyleInstance.Get());
	if (Segments && CurrentSegmentIndex)
	{
		Marshaller->AppendInlineDecorator(MakeShared<FDialogueBoxTextDecorator>(Segments, CurrentSegmentIndex));
	}

	MyRichTextBlock =
		SNew(SRichTextBlock)
		.TextStyle(bOverrideDefaultStyle ? &DefaultTextStyleOverride : &DefaultTextStyle)
		.Marshaller(Marshaller);

	return MyRichTextBlock.ToSharedRef();
}

UDialogueBox::UDialogueBox(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bHasFinishedPlaying = true;
}

void UDialogueBox::PlayLine(const FText& InLine)
{
	check(GetWorld());

	FTimerManager& TimerManager = GetWorld()->GetTimerManager();
	TimerManager.ClearTimer(LetterTimer);

	CurrentLine = InLine;
	CurrentLetterIndex = 0;
	CurrentSegmentIndex = 0;
	MaxLetterIndex = 0;
	Segments.Empty();
	CachedSegmentText.Empty();

	if (CurrentLine.IsEmpty())
	{
		if (IsValid(LineText))
		{
			LineText->SetText(FText::GetEmpty());
		}

		bHasFinishedPlaying = true;
		OnLineFinishedPlaying();

		SetVisibility(ESlateVisibility::Hidden);
	}
	else
	{
		if (IsValid(LineText))
		{
			LineText->SetText(FText::GetEmpty());
		}

		bHasFinishedPlaying = false;

		FTimerDelegate Delegate;
		Delegate.BindUObject(this, &ThisClass::PlayNextLetter);

		TimerManager.SetTimer(LetterTimer, Delegate, LetterPlayTime, true);

		SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
}

void UDialogueBox::SkipToLineEnd()
{
	FTimerManager& TimerManager = GetWorld()->GetTimerManager();
	TimerManager.ClearTimer(LetterTimer);

	CurrentLetterIndex = MaxLetterIndex - 1;
	if (IsValid(LineText))
	{
		LineText->SetText(FText::FromString(CalculateSegments()));
	}

	bHasFinishedPlaying = true;
	OnLineFinishedPlaying();
}

void UDialogueBox::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	LineText->ConfigureFromParent(&Segments, &CurrentSegmentIndex);
}

void UDialogueBox::PlayNextLetter()
{
	if (Segments.IsEmpty())
	{
		CalculateWrappedString();
	}

	FString WrappedString = CalculateSegments();

	// TODO: How do we keep indexing of text i18n-friendly?
	if (CurrentLetterIndex < MaxLetterIndex)
	{
		if (IsValid(LineText))
		{
			LineText->SetText(FText::FromString(WrappedString));
		}

		OnPlayLetter();
		++CurrentLetterIndex;
	}
	else
	{
		if (IsValid(LineText))
		{
			LineText->SetText(FText::FromString(CalculateSegments()));
		}

		FTimerManager& TimerManager = GetWorld()->GetTimerManager();
		TimerManager.ClearTimer(LetterTimer);

		FTimerDelegate Delegate;
		Delegate.BindUObject(this, &ThisClass::SkipToLineEnd);

		TimerManager.SetTimer(LetterTimer, Delegate, EndHoldTime, false);
	}
}

void UDialogueBox::CalculateWrappedString()
{
	if (IsValid(LineText) && LineText->GetTextParser().IsValid())
	{
		TSharedPtr<IRichTextMarkupParser> Parser = LineText->GetTextParser();

		TArray<FTextLineParseResults> Lines;
		FString ProcessedString;
		Parser->Process(Lines, CurrentLine.ToString(), ProcessedString);
		for (int32 LineIdx = 0; LineIdx < Lines.Num(); ++LineIdx)
		{
			const FTextLineParseResults& Line = Lines[LineIdx];
			for (const FTextRunParseResults& Run : Line.Runs)
			{
				Segments.Emplace(
					FDialogueTextSegment
					{
						ProcessedString.Mid(Run.OriginalRange.BeginIndex, Run.OriginalRange.Len()),
						Run
					});
			}

			if (LineIdx != Lines.Num() - 1)
			{
				Segments.Emplace(
					FDialogueTextSegment
					{
						TEXT("\n"),
						FTextRunParseResults(FString(), FTextRange(0, 1))
					});
				++MaxLetterIndex;
			}

			MaxLetterIndex = Line.Range.EndIndex;
		}
	}
}

FString UDialogueBox::CalculateSegments()
{
	while (CurrentSegmentIndex < Segments.Num())
	{
		const FDialogueTextSegment& Segment = Segments[CurrentSegmentIndex];

		int32 SegmentStartIndex = std::max(Segment.RunInfo.OriginalRange.BeginIndex, Segment.RunInfo.ContentRange.BeginIndex);
		CurrentLetterIndex = std::max(CurrentLetterIndex, SegmentStartIndex);

		if (Segment.RunInfo.ContentRange.IsEmpty() ? !Segment.RunInfo.OriginalRange.Contains(CurrentLetterIndex) : !Segment.RunInfo.ContentRange.Contains(CurrentLetterIndex))
		{
			CachedSegmentText += Segment.Text;
			CurrentSegmentIndex++;
			continue;
		}

		// is this segment an inline tag?
		if (!Segment.RunInfo.Name.IsEmpty() && !Segment.RunInfo.MetaData.IsEmpty())
		{
			// seek to end of tag - treat as single character
			int32 SegmentEndIndex = std::max(Segment.RunInfo.OriginalRange.EndIndex, Segment.RunInfo.ContentRange.EndIndex);
			CurrentLetterIndex = std::max(CurrentLetterIndex, SegmentEndIndex);
			return CachedSegmentText + Segment.Text;
		}
		// is this segment partially typed?
		else if (Segment.RunInfo.OriginalRange.Contains(CurrentLetterIndex))
		{
			FString Result = CachedSegmentText + Segment.Text.Mid(0, CurrentLetterIndex - Segment.RunInfo.OriginalRange.BeginIndex);

			// if content tags need closing, append the remaining tag characters
			if (!Segment.RunInfo.ContentRange.IsEmpty() && Segment.RunInfo.ContentRange.Contains(CurrentLetterIndex))
			{
				Result += Segment.Text.Mid(Segment.RunInfo.ContentRange.EndIndex - Segment.RunInfo.OriginalRange.BeginIndex, Segment.RunInfo.OriginalRange.EndIndex - Segment.RunInfo.ContentRange.EndIndex);
			}

			return Result;
		}
		break;
	}

	return CachedSegmentText;
}