// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystemInterface.h"
#include "ModularGameState.h"
#include "Dark_TdoreGameState.generated.h"

class UDark_TdoreAbilitySystemComponent;
class UDark_TdoreGamePhaseAbility;
class UDark_TdoreTeamCreationComponent;

UCLASS()
class ADark_TdoreGameState : public AModularGameStateBase, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ADark_TdoreGameState(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;

	/** GamePhase 初始阶段类（蓝图可覆写为 BP_Phase_PreGame） */
	UPROPERTY(EditDefaultsOnly, Category = "GamePhase")
	TSubclassOf<UDark_TdoreGamePhaseAbility> InitialPhaseClass;

	UPROPERTY(VisibleAnywhere, Category = Abilities)
	TObjectPtr<UDark_TdoreAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Teams)
	TObjectPtr<UDark_TdoreTeamCreationComponent> TeamCreationComponent;
};
