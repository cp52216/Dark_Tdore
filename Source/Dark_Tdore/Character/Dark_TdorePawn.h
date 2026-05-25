// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ModularPawn.h"
#include "AbilitySystemInterface.h"
#include "Dark_TdorePawn.generated.h"

class UDark_TdoreAbilitySystemComponent;
class UDark_TdoreHealthSet;
class UDark_TdoreCombatSet;
class UDark_TdoreAbilitySet;
class UDark_TdoreHealthComponent;
class UDark_TdorePawnExtensionComponent;

/**
 * ADark_TdorePawn — 所有 Pawn 的 GAS 基类（参考 Lyra ALyraPawn）
 *
 * 继承: AModularPawn → APawn
 *
 * AModularPawn 做了什么：
 *  PreInitializeComponents → AddGameFrameworkComponentReceiver
 *  → GameFrameworkComponentManager 知道此 Actor
 *  → 所有组件（PawnExtComp 等）的 InitState 协调才能工作
 *
 * 适用：AI 敌人、NPC、炮塔、载具等不需要 CharacterMovement 的 Pawn
 */
UCLASS(abstract)
class ADark_TdorePawn : public AModularPawn, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ADark_TdorePawn();

	//~IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~End of IAbilitySystemInterface

protected:
	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void OnRep_PlayerState() override;

	/** 设置 ASC（由子类在构造中调用） */
	void SetupAbilitySystem();

	// ============ 蓝图配置：DataAsset ============

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Abilities")
	UDark_TdoreAbilitySet* AbilitySet;

	// ============ 核心组件 ============

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UDark_TdorePawnExtensionComponent* PawnExtComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Abilities", meta = (AllowPrivateAccess = "true"))
	UDark_TdoreAbilitySystemComponent* AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<UDark_TdoreHealthSet> HealthSet;

	/** CombatSet — 战斗属性（BaseDamage/BaseHeal），ExecutionCalculation 读取 */
	UPROPERTY()
	TObjectPtr<UDark_TdoreCombatSet> CombatSet;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UDark_TdoreHealthComponent* HealthComponent;
};
