// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreCombatSet.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreCombatSet)

UDark_TdoreCombatSet::UDark_TdoreCombatSet()
	: BaseDamage(0.0f)
	, BaseHeal(0.0f)
	, Mana(100.0f)
	, MaxMana(100.0f)
{
}

void UDark_TdoreCombatSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UDark_TdoreCombatSet, BaseDamage, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UDark_TdoreCombatSet, BaseHeal, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UDark_TdoreCombatSet, Mana, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UDark_TdoreCombatSet, MaxMana, COND_OwnerOnly, REPNOTIFY_Always);
}

void UDark_TdoreCombatSet::OnRep_BaseDamage(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UDark_TdoreCombatSet, BaseDamage, OldValue);
}

void UDark_TdoreCombatSet::OnRep_BaseHeal(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UDark_TdoreCombatSet, BaseHeal, OldValue);
}

void UDark_TdoreCombatSet::OnRep_Mana(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UDark_TdoreCombatSet, Mana, OldValue);
}

void UDark_TdoreCombatSet::OnRep_MaxMana(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UDark_TdoreCombatSet, MaxMana, OldValue);
}
