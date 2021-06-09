// Copyright (c) Sam Bloomberg

#include "DialogueBox.h"
#include "Engine/Font.h"
#include "Styling/SlateStyle.h"
#include "Widgets/Text/SRichTextBlock.h"

TSharedRef<SWidget> UDialogueTextBlock::RebuildWidget()
{
	// Copied from URichTextBlock::RebuildWidget
	UpdateStyleData();

	TArray< TSharedRef< class ITextDecorator > > CreatedDecorators;
	CreateDecorators(CreatedDecorators);

	TextMarshaller = FRichTextLayoutMarshaller::Create(CreateMarkupParser(), CreateMarkupWriter(), CreatedDecorators, StyleInstance.Get());

	MyRichTextBlock =
		SNew(SRichTextBlock)
		.TextStyle(bOverrideDefaultStyle ? &DefaultTextStyleOverride : &DefaultTextStyle)
		.Marshaller(TextMarshaller)
		.CreateSlateTextLayout(
			FCreateSlateTextLayout::CreateWeakLambda(this, [this] (SWidget* InOwner, const FTextBlockStyle& InDefaultTextStyle) mutable
			{
				TextLayout = FSlateTextLayout::Create(InOwner, InDefaultTextStyle);
				return StaticCastSharedPtr<FSlateTextLayout>(TextLayout).ToSharedRef();
			}));

	return MyRichTextBlock.ToSharedRef();
}

UDialogueBox::UDialogueBox(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bHasFinishedPlaying = true;
}

void UDialogueBox::PlayLine(const FTalkLine& InLine)
{
	check(GetWorld());

	FTimerManager& TimerManager = GetWorld()->GetTimerManager();
	TimerManager.ClearTimer(LetterTimer);

	CurrentLine = InLine;
	CurrentLetterIndex = 0;
	CachedLetterIndex = 0;
	CurrentSegmentIndex = 0;
	MaxLetterIndex = 0;
	Segments.Empty();
	CachedSegmentText.Empty();

	if (CurrentLine.Text.IsEmpty())
	{
		if (IsValid(NameText))
		{
			NameText->SetText(FText::GetEmpty());
		}

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
		if (IsValid(NameText))
		{
			FText SpeakerName;
			if (CurrentLine.SpeakerNameOverride.IsEmpty())
			{
				if (IsValid(CurrentLine.Speaker))
				{
					SpeakerName = CurrentLine.Speaker->Name;
				}
			}
			else
			{
				SpeakerName = CurrentLine.SpeakerNameOverride;
			}

			NameText->SetText(SpeakerName);
		}

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

// TODO: Need to recalculate this + CalculateSegments when the text box gets resized.
void UDialogueBox::CalculateWrappedString()
{
	if (IsValid(LineText) && LineText->GetTextLayout().IsValid())
	{
		TSharedPtr<FSlateTextLayout> Layout = LineText->GetTextLayout();
		TSharedPtr<FRichTextLayoutMarshaller> Marshaller = LineText->GetTextMarshaller();

		const FGeometry& TextBoxGeometry = LineText->GetCachedGeometry();
		const FVector2D TextBoxSize = TextBoxGeometry.GetLocalSize();

		Layout->SetWrappingWidth(TextBoxSize.X);
		Marshaller->SetText(CurrentLine.Text.ToString(), *Layout.Get());
		Layout->UpdateIfNeeded();

		bool bHasWrittenText = false;
		for (const FTextLayout::FLineView& View: Layout->GetLineViews())
		{
			const FTextLayout::FLineModel& Model = Layout->GetLineModels()[View.ModelIndex];

			for (TSharedRef<ILayoutBlock> Block : View.Blocks)
			{
				TSharedRef<IRun> Run = Block->GetRun();

				FDialogueTextSegment Segment;
				Run->AppendTextTo(Segment.Text, Block->GetTextRange());

				// HACK: For some reason image decorators (and possibly other decorators that don't
				// have actual text inside them) result in the run containing a zero width space instead of
				// nothing. This messes up our checks for whether the text is empty or not, which doesn't
				// have an effect on image decorators but might cause issues for other custom ones.
				if (Segment.Text.Len() == 1 && Segment.Text[0] == 0x200B)
				{
					Segment.Text.Empty();
				}

				Segment.RunInfo = Run->GetRunInfo();
				Segments.Add(Segment);

				// A segment with a named run should still take up time for the typewriter effect.
				MaxLetterIndex += FMath::Max(Segment.Text.Len(), Segment.RunInfo.Name.IsEmpty() ? 0 : 1);

				if (!Segment.Text.IsEmpty() || !Segment.RunInfo.Name.IsEmpty())
				{
					bHasWrittenText = true;
				}
			}

			if (bHasWrittenText)
			{
				Segments.Add(FDialogueTextSegment{TEXT("\n")});
				++MaxLetterIndex;
			}
		}

		Layout->SetWrappingWidth(0);
		LineText->SetText(LineText->GetText());
	}
	else
	{
		Segments.Add(FDialogueTextSegment{CurrentLine.Text.ToString()});
		MaxLetterIndex = Segments[0].Text.Len();
	}
}

FString UDialogueBox::CalculateSegments()
{
	FString Result = CachedSegmentText;

	int32 Idx = CachedLetterIndex;
	while (Idx <= CurrentLetterIndex && CurrentSegmentIndex < Segments.Num())
	{
		const FDialogueTextSegment& Segment = Segments[CurrentSegmentIndex];
		if (!Segment.RunInfo.Name.IsEmpty())
		{
			Result += FString::Printf(TEXT("<%s"), *Segment.RunInfo.Name);

			if (!Segment.RunInfo.MetaData.IsEmpty())
			{
				for (const TTuple<FString, FString>& MetaData : Segment.RunInfo.MetaData)
				{
					Result += FString::Printf(TEXT(" %s=\"%s\""), *MetaData.Key, *MetaData.Value);
				}
			}

			if (Segment.Text.IsEmpty())
			{
				Result += TEXT("/>");
				++Idx; // This still takes up an index for the typewriter effect.
			}
			else
			{
				Result += TEXT(">");
			}
		}

		bool bIsSegmentComplete = true;
		if (!Segment.Text.IsEmpty())
		{
			int32 LettersLeft = CurrentLetterIndex - Idx + 1;
			bIsSegmentComplete = LettersLeft >= Segment.Text.Len();
			LettersLeft = FMath::Min(LettersLeft, Segment.Text.Len());
			Idx += LettersLeft;

			Result += Segment.Text.Mid(0, LettersLeft);

			if (!Segment.RunInfo.Name.IsEmpty())
			{
				Result += TEXT("</>");
			}
		}

		if (bIsSegmentComplete)
		{
			CachedLetterIndex = Idx;
			CachedSegmentText = Result;
			++CurrentSegmentIndex;
		}
		else
		{
			break;
		}
	}

	return Result;
}