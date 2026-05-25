// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"

#include "Dark_TdoreAbilityTagRelationshipMapping.generated.h"

class UObject;

// ========================================================================
// FDark_TdoreAbilityTagRelationship — 单个技能标签关系规则（参考 Lyra FLyraAbilityTagRelationship）
// ========================================================================
// 
// 此结构体定义「拥有 AbilityTag 的技能」对其他技能的影响：
//   - AbilityTagsToBlock     → 激活时 Block 这些 Tag 的技能（被 Block 的技能无法激活）
//   - AbilityTagsToCancel    → 激活时 Cancel 这些 Tag 的技能（正在运行的被取消）
//   - ActivationRequiredTags → 这些 Tag 会被隐式添加到该技能的激活必要条件
//   - ActivationBlockedTags  → 这些 Tag 会被隐式添加到该技能的激活阻止条件
//
// 所有关系都在运行时由 ASC 自动处理，GA 代码无需手动编写互斥逻辑。
// 策划通过 DataAsset 蓝图修改关系，不需要改 C++ 代码。
//
// 例子：
//   AbilityTag = "Action.Attack.Light"   ← 所有轻攻击技能
//   AbilityTagsToCancel = "Action.Move.Dash"  ← 轻攻击可以打断闪避
//   ActivationBlockedTags = "Status.Death.Dying"  ← 濒死时不能轻攻击
USTRUCT()
struct FDark_TdoreAbilityTagRelationship
{
	GENERATED_BODY()

	/** 本关系的主标签 — 拥有此标签的技能会应用此关系 */
	UPROPERTY(EditAnywhere, Category = Ability, meta = (Categories = "Gameplay.Action"))
	FGameplayTag AbilityTag;

	/** 激活时阻止这些标签对应的技能激活 */
	UPROPERTY(EditAnywhere, Category = Ability)
	FGameplayTagContainer AbilityTagsToBlock;

	/** 激活时取消这些标签对应的正在运行的技能 */
	UPROPERTY(EditAnywhere, Category = Ability)
	FGameplayTagContainer AbilityTagsToCancel;

	/** 隐式添加为拥有此标签的技能的必要激活标签 */
	UPROPERTY(EditAnywhere, Category = Ability)
	FGameplayTagContainer ActivationRequiredTags;

	/** 隐式添加为拥有此标签的技能的阻止激活标签 */
	UPROPERTY(EditAnywhere, Category = Ability)
	FGameplayTagContainer ActivationBlockedTags;
};

// ========================================================================
// UDark_TdoreAbilityTagRelationshipMapping — 技能标签关系映射表（参考 Lyra ULyraAbilityTagRelationshipMapping）
// ========================================================================
//
// 这是一个蓝图可编辑的 DataAsset，用于配置技能之间的 Block/Cancel 关系。
// 替代在 GA 中硬编码 CancelAbilities() 调用 — 所有关系在此集中配置，ASC 自动应用。
//
// 集成流程：
//   1. 创建 DataAsset: /Game/Abilities/DA_AbilityTagRelationship
//   2. 在 AbilityTagRelationships 数组中添加关系规则
//   3. 在 PawnData 或 PawnExtensionComponent 中引用此 DataAsset
//   4. ASC 初始化时调用 ASC->SetTagRelationshipMapping(DA)
//   5. 技能激活/Block/Cancel 时 ASC 自动查询并应用规则
//
// ASC 调用链（参考 Lyra LyraAbilitySystemComponent）：
//   ApplyAbilityBlockAndCancelTags() → Mapping->GetAbilityTagsToBlockAndCancel()
//   CanActivateAbility() 前隐式调用 GetAdditionalActivationTagRequirements()
UCLASS()
class UDark_TdoreAbilityTagRelationshipMapping : public UDataAsset
{
	GENERATED_BODY()

private:
	/** 技能标签关系规则列表（蓝图 DataAsset 中编辑，TitleProperty 显示 AbilityTag） */
	UPROPERTY(EditAnywhere, Category = Ability, meta = (TitleProperty = "AbilityTag"))
	TArray<FDark_TdoreAbilityTagRelationship> AbilityTagRelationships;

public:
	/** 
	 * 根据技能身上的标签集合，扩展出需要 Block 和 Cancel 的标签。
	 * 调用时机：ASC::ApplyAbilityBlockAndCancelTags() 内部。
	 * 
	 * @param AbilityTags  当前激活技能的标签集合
	 * @param OutTagsToBlock  [输出] 需要阻止激活的标签
	 * @param OutTagsToCancel [输出] 需要取消运行的标签
	 */
	void GetAbilityTagsToBlockAndCancel(const FGameplayTagContainer& AbilityTags,
		FGameplayTagContainer* OutTagsToBlock, FGameplayTagContainer* OutTagsToCancel) const;

	/** 
	 * 根据技能身上的标签集合，扩展激活必要条件和阻止条件。
	 * 调用时机：技能激活前，ASC 查询额外条件。
	 * 
	 * @param AbilityTags          当前技能的标签集合
	 * @param OutActivationRequired [输出] 额外必要的激活标签
	 * @param OutActivationBlocked  [输出] 额外阻止激活的标签
	 */
	void GetRequiredAndBlockedActivationTags(const FGameplayTagContainer& AbilityTags,
		FGameplayTagContainer* OutActivationRequired, FGameplayTagContainer* OutActivationBlocked) const;

	/** 
	 * 检查指定标签集合的技能是否会被 ActionTag 对应的技能取消。
	 * 
	 * @param AbilityTags  被检查的技能标签集合
	 * @param ActionTag    触发取消的动作标签
	 * @return 如果会被取消返回 true
	 */
	bool IsAbilityCancelledByTag(const FGameplayTagContainer& AbilityTags, const FGameplayTag& ActionTag) const;
};
