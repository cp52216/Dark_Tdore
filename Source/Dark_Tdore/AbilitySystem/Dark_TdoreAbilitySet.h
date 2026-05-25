// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "GameplayAbilitySpecHandle.h"
#include "Dark_TdoreAbilitySet.generated.h"

class UGameplayAbility;
class UGameplayEffect;
class UAttributeSet;
class UDark_TdoreAbilitySystemComponent;

/**
 * FDark_TdoreAbilitySet_Ability — 单个技能授予条目
 * 参考 Lyra: FLyraAbilitySet_GameplayAbility
 *
 * 在 DataAsset 蓝图中配置每个技能及其对应的输入标签。
 * 新增技能只需在 DataAsset 加一行，不需改 C++。
 */
USTRUCT(BlueprintType)
struct FDark_TdoreAbilitySet_Ability
{
	GENERATED_BODY()

	/** 要授予的技能类 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSubclassOf<UGameplayAbility> Ability;

	/** 技能等级 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	int32 AbilityLevel = 1;

	/** 输入标签（ASC 通过此标签路由按键到技能，如 Input.Ability.Q） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Meta = (Categories = "Input"))
	FGameplayTag InputTag;
};

/**
 * FDark_TdoreAbilitySet_Effect — 单个 GE 授予条目
 */
USTRUCT(BlueprintType)
struct FDark_TdoreAbilitySet_Effect
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSubclassOf<UGameplayEffect> GameplayEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	float EffectLevel = 1.0f;
};

/**
 * UDark_TdoreAbilitySet — 技能集合 DataAsset
 * 参考 Lyra: ULyraAbilitySet
 *
 * 蓝图可编辑 DataAsset，捆绑技能 + 效果 + 属性。
 * Character 引用此资产，BeginPlay 时自动授予。
 *
 * 使用流程：
 *   1. 创建 DataAsset: /Game/Abilities/DA_DefaultAbilitySet
 *   2. 配置 GrantedAbilities 数组：每行选 Ability 类 + InputTag
 *   3. 在 Character BP 的 AbilitySet 属性中引用此 DataAsset
 */
UCLASS(BlueprintType, Const)
class UDark_TdoreAbilitySet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UDark_TdoreAbilitySet(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** 将此技能集合授予指定 ASC */
	void GiveToAbilitySystem(UDark_TdoreAbilitySystemComponent* ASC, UObject* SourceObject = nullptr) const;

protected:
	/** 要授予的技能列表（每行: 技能类 + 输入标签） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities", meta = (TitleProperty = "Ability"))
	TArray<FDark_TdoreAbilitySet_Ability> GrantedAbilities;

	/** 要授予的 GameplayEffect 列表 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effects", meta = (TitleProperty = "GameplayEffect"))
	TArray<FDark_TdoreAbilitySet_Effect> GrantedEffects;
};
