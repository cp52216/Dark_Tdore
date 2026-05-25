// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ModularPlayerState.h"
#include "AbilitySystemInterface.h"
#include "Dark_TdorePlayerState.generated.h"

class UDark_TdoreAbilitySystemComponent;
class UDark_TdoreHealthSet;
class UDark_TdoreCombatSet;
class UDark_TdorePawnData;

/**
 * ADark_TdorePlayerState — 玩家持久状态（参考 Lyra ALyraPlayerState）
 *
 * 继承: AModularPlayerState → APlayerState
 *
 * 为什么 ASC 放在 PlayerState 而不是 Character？
 *  1. 持久性：Character 死亡重生产生新实例，属性丢失；PlayerState 不销毁，属性保持
 *  2. Owner/Avatar 分离：OwnerActor = PlayerState, AvatarActor = Character（GAS 要求）
 *  3. PawnData 一次性配置：AbilitySet + InputConfig 捆绑，切换角色只需换 DA
 */
UCLASS()
class ADark_TdorePlayerState : public AModularPlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ADark_TdorePlayerState(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	UDark_TdoreAbilitySystemComponent* GetDark_TdoreAbilitySystemComponent() const { return AbilitySystemComponent; }

	/** 获取 PawnData（含 AbilitySets + InputConfig） */
	const UDark_TdorePawnData* GetPawnData() const { return PawnData; }

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;

	/** 遍历 PawnData->AbilitySets，授予 GA/GE */
	void GrantAbilitySets();

	// ============ DataAsset ============

	/** Pawn 配置 DataAsset（参考 Lyra ULyraPawnData） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="PawnData")
	TObjectPtr<const UDark_TdorePawnData> PawnData;

	// ============ GAS 组件 ============

	UPROPERTY(VisibleAnywhere, Category="Abilities")
	UDark_TdoreAbilitySystemComponent* AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<UDark_TdoreHealthSet> HealthSet;

	UPROPERTY()
	TObjectPtr<UDark_TdoreCombatSet> CombatSet;
};
