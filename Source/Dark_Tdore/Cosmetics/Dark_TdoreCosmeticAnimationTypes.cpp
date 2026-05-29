// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreCosmeticAnimationTypes.h"

#include "Animation/AnimInstance.h"
#include "Engine/SkeletalMesh.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreCosmeticAnimationTypes)

// ============================================================
//  FDark_TdoreAnimLayerSelectionSet
// ============================================================

TSubclassOf<UAnimInstance> FDark_TdoreAnimLayerSelectionSet::SelectBestLayer(const FGameplayTagContainer& CosmeticTags) const
{
	// 遍历规则，返回第一个完全匹配的层
	for (const FDark_TdoreAnimLayerSelectionEntry& Rule : LayerRules)
	{
		if ((Rule.Layer != nullptr) && CosmeticTags.HasAll(Rule.RequiredTags))
		{
			return Rule.Layer;
		}
	}

	// 无匹配时返回默认层
	return DefaultLayer;
}

// ============================================================
//  FDark_TdoreAnimBodyStyleSelectionSet
// ============================================================

USkeletalMesh* FDark_TdoreAnimBodyStyleSelectionSet::SelectBestBodyStyle(const FGameplayTagContainer& CosmeticTags) const
{
	// 遍历规则，返回第一个完全匹配的网格体
	for (const FDark_TdoreAnimBodyStyleSelectionEntry& Rule : MeshRules)
	{
		if ((Rule.Mesh != nullptr) && CosmeticTags.HasAll(Rule.RequiredTags))
		{
			return Rule.Mesh;
		}
	}

	// 无匹配时返回默认网格体
	return DefaultMesh;
}
