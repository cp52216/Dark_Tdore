// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Dark_TdoreCombatTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreCombatTypes)

bool UDark_TdoreComboData::FindStepForInputTag(FGameplayTag InputTag, FDark_TdoreComboStep& OutStep) const
{
	if (!InputTag.IsValid())
	{
		return false;
	}

	for (const FDark_TdoreComboStep& Step : Steps)
	{
		if (Step.InputTag == InputTag)
		{
			OutStep = Step;
			return true;
		}
	}

	return false;
}

bool UDark_TdoreComboData::FindStepByName(FName StepName, FDark_TdoreComboStep& OutStep) const
{
	if (StepName.IsNone())
	{
		return false;
	}

	for (const FDark_TdoreComboStep& Step : Steps)
	{
		if (Step.StepName == StepName)
		{
			OutStep = Step;
			return true;
		}
	}

	return false;
}

