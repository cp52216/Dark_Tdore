// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"

#include "Dark_TdoreInputConfig.generated.h"

class UInputAction;

/**
 * FDark_TdoreInputAction
 *
 *	将 InputAction 映射到 GameplayTag（参考 Lyra FLyraInputAction）
 */
USTRUCT(BlueprintType)
struct FDark_TdoreInputAction
{
	GENERATED_BODY()

public:

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<const UInputAction> InputAction = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Meta = (Categories = "InputTag"))
	FGameplayTag InputTag;
};


/**
 * UDark_TdoreInputConfig
 *
 *	不可变 DataAsset，集中管理所有输入配置（参考 Lyra ULyraInputConfig）
 *
 *	NativeInputActions：手动绑定的原生动作（Move, Look, Jump）
 *	AbilityInputActions：自动绑定到拥有匹配 InputTag 的 GA
 */
UCLASS(BlueprintType, Const)
class UDark_TdoreInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:

	UDark_TdoreInputConfig(const FObjectInitializer& ObjectInitializer);

	/** 通过 GameplayTag 查找 Native 输入动作 */
	UFUNCTION(BlueprintCallable, Category = "DarkTdore|Input")
	const UInputAction* FindNativeInputActionForTag(const FGameplayTag& InputTag, bool bLogNotFound = true) const;

	/** 通过 GameplayTag 查找 Ability 输入动作 */
	UFUNCTION(BlueprintCallable, Category = "DarkTdore|Input")
	const UInputAction* FindAbilityInputActionForTag(const FGameplayTag& InputTag, bool bLogNotFound = true) const;

public:

	/** 原生输入动作（Move, Look, Jump 等，需手动绑定到回调函数） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Meta = (TitleProperty = "InputAction"))
	TArray<FDark_TdoreInputAction> NativeInputActions;

	/** 技能输入动作（自动触发拥有匹配 InputTag 的 GA） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Meta = (TitleProperty = "InputAction"))
	TArray<FDark_TdoreInputAction> AbilityInputActions;
};
