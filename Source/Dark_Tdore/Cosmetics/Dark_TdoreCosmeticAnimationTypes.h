// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"
#include "Templates/SubclassOf.h"

#include "Dark_TdoreCosmeticAnimationTypes.generated.h"

class UAnimInstance;
class UPhysicsAsset;
class USkeletalMesh;

// ============================================================
//  动画层选择 — 基于 GameplayTag 驱动（参考 Lyra FLyraAnimLayerSelectionSet）
// ============================================================

/**
 * 单个动画层选择规则
 * 如果角色的 Cosmetic Tags 包含 RequiredTags 中的所有标签，则使用此 Layer
 */
USTRUCT(BlueprintType)
struct FDark_TdoreAnimLayerSelectionEntry
{
	GENERATED_BODY()

	/** 匹配后应用的动画层（AnimInstance 子类） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSubclassOf<UAnimInstance> Layer;

	/** 匹配条件：必须全部满足的 Cosmetic 标签 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Categories = "Cosmetic"))
	FGameplayTagContainer RequiredTags;
};

/**
 * 动画层选择器
 *
 * 使用方式（在 PawnData 或 WeaponInstance 中配置）：
 *   LayerRules:
 *     [0] Layer = ABP_PistolAnimLayers,  RequiredTags = Cosmetic.Weapon.Pistol
 *     [1] Layer = ABP_RifleAnimLayers,   RequiredTags = Cosmetic.Weapon.Rifle
 *   DefaultLayer = ABP_UnarmedAnimLayers
 *
 * 运行时通过 SelectBestLayer(CosmeticTags) 获取最佳动画层，
 * 然后通过 Linked Anim Layers 节点动态替换动画。
 */
USTRUCT(BlueprintType)
struct FDark_TdoreAnimLayerSelectionSet
{
	GENERATED_BODY()

	/** 规则列表，按顺序匹配（第一个满足的生效） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (TitleProperty = Layer))
	TArray<FDark_TdoreAnimLayerSelectionEntry> LayerRules;

	/** 当所有规则都不匹配时使用的默认动画层 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSubclassOf<UAnimInstance> DefaultLayer;

	/** 根据 CosmeticTags 选择最匹配的动画层 */
	TSubclassOf<UAnimInstance> SelectBestLayer(const FGameplayTagContainer& CosmeticTags) const;
};

// ============================================================
//  身体风格选择 — 基于 GameplayTag 驱动的骨骼网格体选择
// ============================================================

/**
 * 单个身体风格选择规则
 * 例如：根据 Cosmetic.Gender.Male/Female 选择不同的骨骼网格体
 */
USTRUCT(BlueprintType)
struct FDark_TdoreAnimBodyStyleSelectionEntry
{
	GENERATED_BODY()

	/** 匹配后使用的骨骼网格体 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<USkeletalMesh> Mesh = nullptr;

	/** 匹配条件：必须全部满足的 Cosmetic 标签 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Categories = "Cosmetic"))
	FGameplayTagContainer RequiredTags;
};

/**
 * 身体风格选择器
 *
 * 使用方式：
 *   MeshRules:
 *     [0] Mesh = SK_Manny,      RequiredTags = Cosmetic.Body.Masculine
 *     [1] Mesh = SK_Quinn,     RequiredTags = Cosmetic.Body.Feminine
 *   DefaultMesh = SK_Manny
 *   ForcedPhysicsAsset = PA_Default  (可选，强制使用此物理资产)
 */
USTRUCT(BlueprintType)
struct FDark_TdoreAnimBodyStyleSelectionSet
{
	GENERATED_BODY()

	/** 规则列表，按顺序匹配 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (TitleProperty = Mesh))
	TArray<FDark_TdoreAnimBodyStyleSelectionEntry> MeshRules;

	/** 默认骨骼网格体 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<USkeletalMesh> DefaultMesh = nullptr;

	/** 可选的强制物理资产（如果设置，始终使用此物理资产而非 Mesh 自带的） */
	UPROPERTY(EditAnywhere)
	TObjectPtr<UPhysicsAsset> ForcedPhysicsAsset = nullptr;

	/** 根据 CosmeticTags 选择最匹配的骨骼网格体 */
	USkeletalMesh* SelectBestBodyStyle(const FGameplayTagContainer& CosmeticTags) const;
};
