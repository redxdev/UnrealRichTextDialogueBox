#pragma once

#include <Widgets/Text/SRichTextBlock.h>

class SDialogueTextBlock : public SRichTextBlock
{
public:
	TAttribute<FText> MakeTextAttribute(const FText& typedText, const FText& finalText) const;

protected:
	void CacheDesiredSize(float LayoutScaleMultiplier) override;
	FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	FText GetTextInternal(FText typedText, FText finalText) const;

	mutable bool isComputingDesiredSize;
	FVector2D m_cachedDesiredSize;
};
