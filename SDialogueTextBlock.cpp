#include "SDialogueTextBlock.h"

TAttribute<FText> SDialogueTextBlock::MakeTextAttribute(const FText& typedText, const FText& finalText) const
{
	return TAttribute<FText>::CreateRaw(this, &SDialogueTextBlock::GetTextInternal, typedText, finalText);
}

FVector2D SDialogueTextBlock::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return m_cachedDesiredSize;
}

void SDialogueTextBlock::CacheDesiredSize(float LayoutScaleMultiplier)
{
	// calculate actual maxmimum dialogue size
	isComputingDesiredSize = true;
	m_cachedDesiredSize = SRichTextBlock::ComputeDesiredSize(LayoutScaleMultiplier);
	isComputingDesiredSize = false;

	// poke the method again because this internally caches some junk pertaining to layout/content
	(void)SRichTextBlock::ComputeDesiredSize(LayoutScaleMultiplier);

	SRichTextBlock::CacheDesiredSize(LayoutScaleMultiplier);
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
