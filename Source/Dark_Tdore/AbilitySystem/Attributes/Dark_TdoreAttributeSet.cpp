// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreAttributeSet.h"
#include "AbilitySystem/Dark_TdoreAbilitySystemComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreAttributeSet)

UDark_TdoreAttributeSet::UDark_TdoreAttributeSet()
{
}

UWorld* UDark_TdoreAttributeSet::GetWorld() const
{
	const UObject* Outer = GetOuter();
	return Outer ? Outer->GetWorld() : nullptr;
}

UDark_TdoreAbilitySystemComponent* UDark_TdoreAttributeSet::GetDarkTdoreAbilitySystemComponent() const
{
	return Cast<UDark_TdoreAbilitySystemComponent>(GetOwningAbilitySystemComponent());
}
