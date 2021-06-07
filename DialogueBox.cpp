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
	MaxLetterIndex = 0;
	Segments.Empty();

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
				Segment.RunInfo = Run->GetRunInfo();
				Segments.Add(Segment);
				MaxLetterIndex += Segment.Text.Len();

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
	FString Result;

	int32 Idx = 0;
	int32 SegmentIdx = 0;
	while (Idx <= CurrentLetterIndex && SegmentIdx < Segments.Num())
	{
		const FDialogueTextSegment& Segment = Segments[SegmentIdx];
		if (!Segment.RunInfo.Name.IsEmpty())
		{
			Result += TEXT("<") + Segment.RunInfo.Name + TEXT(">");
		}

		int32 LettersLeft = CurrentLetterIndex - Idx + 1;
		LettersLeft = FMath::Min(LettersLeft, Segment.Text.Len());
		Idx += LettersLeft;
		Result += Segment.Text.Mid(0, LettersLeft);

		if (!Segment.RunInfo.Name.IsEmpty())
		{
			Result += TEXT("</>");
		}

		++SegmentIdx;
	}

	return Result;
}
