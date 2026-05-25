// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystemComponent.h"
#include "Dark_TdoreAbilitySystemComponent.generated.h"

// 前置声明
class UDark_TdoreGameplayAbility;
class UDark_TdoreAbilityTagRelationshipMapping;

/**
 * EDark_TdoreAbilityActivationPolicy — 技能激活策略
 *
 * 定义技能以何种方式被激活。
 * 参考 Lyra: ELyraAbilityActivationPolicy
 *
 * OnInputTriggered  — 按下输入时激活（如：跳跃、射击）
 * WhileInputActive  — 按住输入时持续激活（如：冲刺、蓄力）
 * OnSpawn           — 角色生成时自动激活（如：被动光环）
 */
UENUM(BlueprintType)
enum class EDark_TdoreAbilityActivationPolicy : uint8
{
	// 按下输入时尝试激活技能（最常用的模式）
	OnInputTriggered,

	// 按住输入期间持续尝试激活技能（适合蓄力/持续型技能）
	WhileInputActive,

	// 角色/Pawn 生成时自动激活（适合被动技能/光环）
	OnSpawn
};

/**
 * EDark_TdoreAbilityActivationGroup — 技能激活组（互斥控制）
 *
 * 定义技能之间的激活关系。
 * 参考 Lyra: ELyraAbilityActivationGroup
 *
 * Independent           — 独立运行，不与其他技能冲突
 * Exclusive_Replaceable — 互斥可替换：新技能会取消同组旧技能
 * Exclusive_Blocking    — 互斥阻塞：激活后阻止同组其他技能激活
 */
UENUM(BlueprintType)
enum class EDark_TdoreAbilityActivationGroup : uint8
{
	// 独立运行，不与其他技能产生互斥关系（如：跳跃、冲刺可同时存在）
	Independent,

	// 互斥可替换：同组新技能激活时会取消旧技能（如：武器切换）
	Exclusive_Replaceable,

	// 互斥阻塞：激活后阻止同组其他技能激活（如：大招期间禁止普通攻击）
	Exclusive_Blocking,

	MAX UMETA(Hidden)
};

/**
 * UDark_TdoreAbilitySystemComponent — 自定义能力系统组件（ASC）
 *
 * 本项目的核心 ASC 类，基于 UE 原生 UAbilitySystemComponent 扩展。
 * 参考 Lyra: ULyraAbilitySystemComponent
 *
 * 核心功能：
 *   - 输入路由：通过 GameplayTag 将按键输入路由到对应技能
 *   - 输入缓冲：每帧处理缓冲的按键输入（ProcessAbilityInput）
 *   - 激活组管理：支持 Independent / Exclusive 互斥模式
 *   - TagRelationMapping：DataAsset 驱动的技能 Block/Cancel 关系表
 *   - 扩展 EffectContext：自定义 FDark_TdoreGameplayEffectContext
 *   - OnSpawn 技能：角色生成时自动激活标记为 OnSpawn 的技能
 *   - 动态标签效果：通过 GameplayEffect 动态添加/移除 GameplayTag
 *
 * 数据流：
 *   Q 键按下 → Character::OnAbilityInputPressed(Input.Ability.Q)
 *           → ASC::AbilityInputTagPressed("Input.Ability.Q")
 *           → 遍历 ActivatableAbilities，匹配 GetDynamicSpecSourceTags
 *           → 加入 InputPressedSpecHandles 缓冲队列
 *   PlayerController::PostProcessInput → ProcessAbilityInput()
 *        → 处理缓冲队列 → TryActivateAbility(GA_TestQ)
 */
UCLASS()
class UDark_TdoreAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UDark_TdoreAbilitySystemComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~UActorComponent 接口
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ============ 输入路由（从 Character 输入处理器调用） ============

	/** 按键按下：通过 GameplayTag 路由，将匹配的技能加入按下队列 */
	void AbilityInputTagPressed(const FGameplayTag& InputTag);

	/** 按键释放：将 WhileInputActive 类型的技能加入释放队列 */
	void AbilityInputTagReleased(const FGameplayTag& InputTag);

	/** 每帧处理：消费缓冲的输入队列，激活/取消对应技能 */
	void ProcessAbilityInput(float DeltaTime, bool bGamePaused);

	/** 清空所有输入缓冲（角色死亡/状态切换时调用） */
	void ClearAbilityInput();

	// ============ 激活组管理（互斥控制） ============

	/** 检查指定激活组是否被阻塞（Exclusive_Blocking 组中有活跃技能时返回 true） */
	bool IsActivationGroupBlocked(EDark_TdoreAbilityActivationGroup Group) const;

	/** 技能激活时调用：将技能加入对应激活组，计数 +1 */
	void AddAbilityToActivationGroup(EDark_TdoreAbilityActivationGroup Group, UDark_TdoreGameplayAbility* Ability);

	/** 技能结束时调用：将技能移出激活组，计数 -1 */
	void RemoveAbilityFromActivationGroup(EDark_TdoreAbilityActivationGroup Group, UDark_TdoreGameplayAbility* Ability);

	/** 取消指定激活组中的所有技能（可指定忽略某个技能） */
	void CancelActivationGroupAbilities(EDark_TdoreAbilityActivationGroup Group, UDark_TdoreGameplayAbility* IgnoreAbility, bool bReplicateCancelAbility);

	// ============ OnSpawn 技能 ============

	/** 尝试激活所有激活策略为 OnSpawn 的技能（角色生成后调用） */
	void TryActivateAbilitiesOnSpawn();

	// ============ 技能标签关系映射（TagRelationshipMapping）============
	// 参考 Lyra ULyraAbilitySystemComponent 的同名方法

	/** 设置标签关系映射表（通常在 PawnData/ASC 初始化时调用） */
	void SetTagRelationshipMapping(UDark_TdoreAbilityTagRelationshipMapping* NewMapping);

	/** 根据技能标签查询额外的必要/阻止激活条件（技能激活前隐式调用） */
	void GetAdditionalActivationTagRequirements(const FGameplayTagContainer& AbilityTags,
		FGameplayTagContainer& OutActivationRequired, FGameplayTagContainer& OutActivationBlocked) const;

	// ============ 动态标签效果 ============

	/** 通过 GameplayEffect 动态添加 GameplayTag */
	void AddDynamicTagGameplayEffect(const FGameplayTag& Tag);

	/** 移除之前通过 AddDynamicTagGameplayEffect 添加的 GameplayTag */
	void RemoveDynamicTagGameplayEffect(const FGameplayTag& Tag);

protected:
	// 引擎回调：特定技能收到输入按下
	virtual void AbilitySpecInputPressed(FGameplayAbilitySpec& Spec) override;
	// 引擎回调：特定技能收到输入释放
	virtual void AbilitySpecInputReleased(FGameplayAbilitySpec& Spec) override;

	// 引擎回调：技能激活时触发（用于激活组计数 +1）
	virtual void NotifyAbilityActivated(const FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability) override;
	// 引擎回调：技能结束时触发（用于激活组计数 -1）
	virtual void NotifyAbilityEnded(FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability, bool bWasCancelled) override;

	// 引擎回调：应用 Block/Cancel 标签时 — 通过 TagRelationshipMapping 扩展
	virtual void ApplyAbilityBlockAndCancelTags(const FGameplayTagContainer& AbilityTags, UGameplayAbility* RequestingAbility,
		bool bEnableBlockTags, const FGameplayTagContainer& BlockTags,
		bool bExecuteCancelTags, const FGameplayTagContainer& CancelTags) override;

protected:
	/** 本帧按下输入的技能句柄列表 */
	TArray<FGameplayAbilitySpecHandle> InputPressedSpecHandles;

	/** 本帧释放输入的技能句柄列表 */
	TArray<FGameplayAbilitySpecHandle> InputReleasedSpecHandles;

	/** 当前按住输入的技能句柄列表（用于 WhileInputActive 模式） */
	TArray<FGameplayAbilitySpecHandle> InputHeldSpecHandles;

	/** 每个激活组中当前活跃的技能数量（用于互斥判断） */
	int32 ActivationGroupCounts[static_cast<uint8>(EDark_TdoreAbilityActivationGroup::MAX)];

	/** 技能标签关系映射表（DataAsset 驱动，查询 Block/Cancel/Required/Blocked 关系） */
	UPROPERTY()
	TObjectPtr<UDark_TdoreAbilityTagRelationshipMapping> TagRelationshipMapping;
};
