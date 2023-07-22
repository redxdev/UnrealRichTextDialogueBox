#include "SDialogueTextBlock.h"

TAttribute<FText> SDialogueTextBlock::MakeTextAttribute(const FText& typedText, const FText& finalText) const
{
	return TAttribute<FText>::CreateRaw(this, &SDialogueTextBlock::GetTextInternal, typedText, finalText);
}

FVector2D SDialogueTextBlock::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	// calculate actual maxmimum dialogue size
	isComputingDesiredSize = true;
	auto result = SRichTextBlock::ComputeDesiredSize(LayoutScaleMultiplier);
	isComputingDesiredSize = false;

	// poke the method again because this internally caches some junk pertaining to layout/content
	(void)SRichTextBlock::ComputeDesiredSize(LayoutScaleMultiplier);

	// return the overridden value
	return result;
}

FText SDialogueTextBlock::GetTextInternal(FText typedText, FText finalText) const
{
	if (isComputingDesiredSize)
	{
		return finalText;
	}
	else
	{
		return typedText;
	}
}
