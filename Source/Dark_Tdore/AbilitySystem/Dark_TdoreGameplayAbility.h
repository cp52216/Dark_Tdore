// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Abilities/GameplayAbility.h"
#include "Dark_TdoreAbilitySystemComponent.h"
#include "Dark_TdoreGameplayAbility.generated.h"

class UDark_TdoreAbilityCost;

/**
 * UDark_TdoreGameplayAbility — 技能基类
 *
 * 本项目的所有 GameplayAbility 都应继承此类。
 * 参考 Lyra: ULyraGameplayAbility
 *
 * 核心功能：
 *   - ActivationPolicy 激活策略：控制技能何时激活（按键按下/按住/生成时）
 *   - ActivationGroup 激活组：控制技能之间的互斥关系
 *   - 蓝图事件回调：OnAbilityAdded / OnAbilityRemoved / OnPawnAvatarSet
 *   - OnSpawn 自动激活：标记为 OnSpawn 的技能在授予后立即激活
 *   - 激活组阻塞检查：CanActivateAbility 中检查 Exclusive_Blocking 组
 *
 * 使用示例（子类在构造函数中配置）：
 *   // 普通按键技能
 *   ActivationPolicy = EDark_TdoreAbilityActivationPolicy::OnInputTriggered;
 *   ActivationGroup = EDark_TdoreAbilityActivationGroup::Independent;
 *
 *   // 蓄力技能（按住期间激活）
 *   ActivationPolicy = EDark_TdoreAbilityActivationPolicy::WhileInputActive;
 *
 *   // 被动光环（生成时自动激活）
 *   ActivationPolicy = EDark_TdoreAbilityActivationPolicy::OnSpawn;
 */
UCLASS(Abstract, HideCategories = Input, Meta = (ShortTooltip = "Dark_Tdore 项目的 GameplayAbility 基类"))
class UDark_TdoreGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()
	friend class UDark_TdoreAbilitySystemComponent;

public:
	UDark_TdoreGameplayAbility(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** 获取所属的 UDark_TdoreAbilitySystemComponent（蓝图可调用） */
	UFUNCTION(BlueprintCallable, Category = "DarkTdore|Ability")
	UDark_TdoreAbilitySystemComponent* GetDarkTdoreAbilitySystemComponentFromActorInfo() const;

	/** 获取此技能的激活策略 */
	EDark_TdoreAbilityActivationPolicy GetActivationPolicy() const { return ActivationPolicy; }

	/** 获取此技能的激活组 */
	EDark_TdoreAbilityActivationGroup GetActivationGroup() const { return ActivationGroup; }

	/** 尝试在角色生成时激活此技能（OnSpawn 策略专用） */
	void TryActivateAbilityOnSpawn(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) const;

	//~UGameplayAbility 接口覆写
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr, OUT FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual bool CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, OUT FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const override;
	virtual void OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;
	virtual void OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	/** Pawn 化身设置完成时调用（ASC->InitAbilityActorInfo 之后） */
	virtual void OnPawnAvatarSet();

	// ============ BlueprintImplementableEvent 回调（可在蓝图中覆写） ============

	/** 当此技能被授予给 ASC 时调用 */
	UFUNCTION(BlueprintImplementableEvent, Category = Ability, DisplayName = "OnAbilityAdded")
	void K2_OnAbilityAdded();

	/** 当此技能从 ASC 中移除时调用 */
	UFUNCTION(BlueprintImplementableEvent, Category = Ability, DisplayName = "OnAbilityRemoved")
	void K2_OnAbilityRemoved();

	/** 当 ASC 与 Pawn 化身初始化完成时调用 */
	UFUNCTION(BlueprintImplementableEvent, Category = Ability, DisplayName = "OnPawnAvatarSet")
	void K2_OnPawnAvatarSet();

protected:
	/**
	 * 激活策略 — 定义此技能以何种方式激活
	 *
	 * OnInputTriggered  — 按下按键时激活（默认）
	 * WhileInputActive  — 按住按键期间持续激活
	 * OnSpawn           — 角色生成时自动激活
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability Activation")
	EDark_TdoreAbilityActivationPolicy ActivationPolicy = EDark_TdoreAbilityActivationPolicy::OnInputTriggered;

	/**
	 * 激活组 — 定义此技能与其他技能的互斥关系
	 *
	 * Independent           — 独立运行，不冲突
	 * Exclusive_Replaceable — 新技能会取消同组旧技能
	 * Exclusive_Blocking    — 激活后阻止同组新技能激活
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability Activation")
	EDark_TdoreAbilityActivationGroup ActivationGroup = EDark_TdoreAbilityActivationGroup::Independent;

	/** 技能额外消耗列表（参考 Lyra AdditionalCosts） */
	UPROPERTY(EditDefaultsOnly, Instanced, Category = Costs)
	TArray<TObjectPtr<UDark_TdoreAbilityCost>> AdditionalCosts;
};
