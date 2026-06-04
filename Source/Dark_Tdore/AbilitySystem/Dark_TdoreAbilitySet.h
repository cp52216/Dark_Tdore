// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayEffectTypes.h"
#include "Dark_TdoreAbilitySet.generated.h"

class UGameplayAbility;
class UGameplayEffect;
class UAttributeSet;
class UDark_TdoreAbilitySystemComponent;

USTRUCT(BlueprintType)
struct FDark_TdoreAbilitySet_GrantedHandles
{
	GENERATED_BODY()

public:
	/** 记录 GiveAbility 返回的技能句柄，装备卸下时用 ClearAbility 回收。 */
	void AddAbilitySpecHandle(const FGameplayAbilitySpecHandle& Handle);
	/** 记录 ApplyGameplayEffectSpecToSelf 返回的 GE 句柄，装备卸下时移除。 */
	void AddGameplayEffectHandle(const FActiveGameplayEffectHandle& Handle);
	/** 从 ASC 中移除本 AbilitySet 曾经授予的所有技能和 GE。 */
	void TakeFromAbilitySystem(UDark_TdoreAbilitySystemComponent* ASC);

protected:
	/** 该 AbilitySet 授予出去的技能句柄集合。 */
	UPROPERTY()
	TArray<FGameplayAbilitySpecHandle> AbilitySpecHandles;

	/** 该 AbilitySet 应用到 ASC 身上的 GameplayEffect 句柄集合。 */
	UPROPERTY()
	TArray<FActiveGameplayEffectHandle> GameplayEffectHandles;
};

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

	/** 输入标签（ASC 通过此标签路由按键到技能，如 InputTag.Ability.Weapon.2） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Meta = (Categories = "InputTag"))
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

	/**
	 * 将此技能集合授予指定 ASC，并把授予句柄写入 OutGrantedHandles。
	 *
	 * 装备系统使用这个重载：
	 * - 装备时授予武器技能。
	 * - 卸下时通过句柄精确移除这些技能和 GE。
	 */
	void GiveToAbilitySystem(UDark_TdoreAbilitySystemComponent* ASC, FDark_TdoreAbilitySet_GrantedHandles* OutGrantedHandles, UObject* SourceObject = nullptr) const;

protected:
	/** 要授予的技能列表（每行: 技能类 + 输入标签） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities", meta = (TitleProperty = "Ability"))
	TArray<FDark_TdoreAbilitySet_Ability> GrantedAbilities;

	/** 要授予的 GameplayEffect 列表 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effects", meta = (TitleProperty = "GameplayEffect"))
	TArray<FDark_TdoreAbilitySet_Effect> GrantedEffects;
};
