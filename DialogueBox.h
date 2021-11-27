// Copyright (c) Sam Bloomberg

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/RichTextBlock.h"
#include "Framework/Text/RichTextLayoutMarshaller.h"
#include "Framework/Text/SlateTextLayout.h"
#include "DialogueBox.generated.h"

/**
 * A text block that exposes more information about text layout.
 */
UCLASS()
class UDialogueTextBlock : public URichTextBlock
{
	GENERATED_BODY()

public:
	FORCEINLINE TSharedPtr<FSlateTextLayout> GetTextLayout() const
	{
		return TextLayout;
	}

	FORCEINLINE TSharedPtr<FRichTextLayoutMarshaller> GetTextMarshaller() const
	{
		return TextMarshaller;
	}

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TSharedPtr<FSlateTextLayout> TextLayout;
	TSharedPtr<FRichTextLayoutMarshaller> TextMarshaller;
};

struct FDialogueTextSegment
{
	FString Text;
	FRunInfo RunInfo;
};

UCLASS()
class FLAME_API UDialogueBox : public UUserWidget
{
	GENERATED_BODY()

public:
	UDialogueBox(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UDialogueTextBlock> LineText;

	// The amount of time between printing individual letters (for the "typewriter" effect).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue Box")
	float LetterPlayTime = 0.025f;

	// The amount of time to wait after finishing the line before actually marking it completed.
	// This helps prevent accidentally progressing dialogue on short lines.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue Box")
	float EndHoldTime = 0.15f;

	UFUNCTION(BlueprintCallable, Category = "Dialogue Box")
	void PlayLine(const FText& InLine);

	UFUNCTION(BlueprintCallable, Category = "Dialogue Box")
	void GetCurrentLine(FText& OutLine) const { OutLine = CurrentLine; }

	UFUNCTION(BlueprintCallable, Category = "Dialogue Box")
	bool HasFinishedPlayingLine() const { return bHasFinishedPlaying; }

	UFUNCTION(BlueprintCallable, Category = "Dialogue Box")
	void SkipToLineEnd();

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "Dialogue Box")
	void OnPlayLetter();

	UFUNCTION(BlueprintImplementableEvent, Category = "Dialogue Box")
	void OnLineFinishedPlaying();

private:
	void PlayNextLetter();

	void CalculateWrappedString();
	FString CalculateSegments();

	UPROPERTY()
	FText CurrentLine;

	TArray<FDialogueTextSegment> Segments;

	// The section of the text that's already been printed out and won't ever change.
	// This lets us cache some of the work we've already done. We can't cache absolutely
	// everything as the last few characters of a string may change if they're related to
	// a named run that hasn't been completed yet.
	FString CachedSegmentText;
	int32 CachedLetterIndex = 0;

	int32 CurrentSegmentIndex = 0;
	int32 CurrentLetterIndex = 0;
	int32 MaxLetterIndex = 0;

	uint32 bHasFinishedPlaying : 1;

	FTimerHandle LetterTimer;
};