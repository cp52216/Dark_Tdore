// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreAbilitySystemGlobals.h"
#include "Dark_TdoreGameplayEffectContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreAbilitySystemGlobals)

UDark_TdoreAbilitySystemGlobals::UDark_TdoreAbilitySystemGlobals(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FGameplayEffectContext* UDark_TdoreAbilitySystemGlobals::AllocGameplayEffectContext() const
{
	return new FDark_TdoreGameplayEffectContext();
}
