// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreAbilityTagRelationshipMapping.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreAbilityTagRelationshipMapping)

// ============ GetAbilityTagsToBlockAndCancel — 扩展 Block/Cancel 标签 ============
// 参考 Lyra ULyraAbilityTagRelationshipMapping::GetAbilityTagsToBlockAndCancel
//
// 遍历所有关系规则，如果当前技能拥有某个规则中定义的 AbilityTag，
// 则将该规则中的 AbilityTagsToBlock 和 AbilityTagsToCancel 合并到输出。
//
// 使用场景：
//   轻攻击（Action.Attack.Light）→ 配置 Cancel = Action.Move.Dash
//   当轻攻击激活时 ASC 调用此方法 → 自动取消所有带有 Action.Move.Dash 的闪避技能
void UDark_TdoreAbilityTagRelationshipMapping::GetAbilityTagsToBlockAndCancel(
	const FGameplayTagContainer& AbilityTags,
	FGameplayTagContainer* OutTagsToBlock,
	FGameplayTagContainer* OutTagsToCancel) const
{
	for (const FDark_TdoreAbilityTagRelationship& Tags : AbilityTagRelationships)
	{
		// 只有当 AbilityTags 中包含此关系的 AbilityTag 时，才应用此规则
		if (AbilityTags.HasTag(Tags.AbilityTag))
		{
			if (OutTagsToBlock)
			{
				OutTagsToBlock->AppendTags(Tags.AbilityTagsToBlock);
			}
			if (OutTagsToCancel)
			{
				OutTagsToCancel->AppendTags(Tags.AbilityTagsToCancel);
			}
		}
	}
}

// ============ GetRequiredAndBlockedActivationTags — 扩展激活条件 ============
// 参考 Lyra ULyraAbilityTagRelationshipMapping::GetRequiredAndBlockedActivationTags
//
// 遍历所有关系规则，将 ActivationRequiredTags 和 ActivationBlockedTags 合并到输出。
// 这些标签会被隐式添加到技能的激活条件中，无需在每个 GA 上单独配置。
//
// 使用场景：
//   轻攻击（Action.Attack.Light）→ 配置 ActivationBlockedTags = Status.Death.Dying
//   当轻攻击激活前 ASC 调用此方法 → 若角色拥有 Status.Death.Dying 标签则无法激活
void UDark_TdoreAbilityTagRelationshipMapping::GetRequiredAndBlockedActivationTags(
	const FGameplayTagContainer& AbilityTags,
	FGameplayTagContainer* OutActivationRequired,
	FGameplayTagContainer* OutActivationBlocked) const
{
	for (const FDark_TdoreAbilityTagRelationship& Tags : AbilityTagRelationships)
	{
		if (AbilityTags.HasTag(Tags.AbilityTag))
		{
			if (OutActivationRequired)
			{
				OutActivationRequired->AppendTags(Tags.ActivationRequiredTags);
			}
			if (OutActivationBlocked)
			{
				OutActivationBlocked->AppendTags(Tags.ActivationBlockedTags);
			}
		}
	}
}

// ============ IsAbilityCancelledByTag — 判断是否会被取消 ============
// 参考 Lyra ULyraAbilityTagRelationshipMapping::IsAbilityCancelledByTag
//
// 检查：ActionTag 对应的关系规则中，AbilityTagsToCancel 是否包含 AbilityTags 中的任一个。
// 返回 true 表示 AbilityTags 对应的技能会被 ActionTag 对应的技能取消。
//
// 使用场景：UI 显示「正在发动的技能会被即将激活的技能打断」警告
bool UDark_TdoreAbilityTagRelationshipMapping::IsAbilityCancelledByTag(
	const FGameplayTagContainer& AbilityTags,
	const FGameplayTag& ActionTag) const
{
	for (const FDark_TdoreAbilityTagRelationship& Tags : AbilityTagRelationships)
	{
		// 找到 ActionTag 对应的关系 → 检查当前运行的技能标签是否在 Cancel 列表中
		if (Tags.AbilityTag == ActionTag && Tags.AbilityTagsToCancel.HasAny(AbilityTags))
		{
			return true;
		}
	}

	return false;
}
