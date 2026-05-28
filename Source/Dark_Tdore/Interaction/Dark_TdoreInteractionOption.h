// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Dark_TdoreInteractionOption.generated.h"

class IDark_TdoreInteractableTarget;
class UUserWidget;

/**
 * FDark_TdoreInteractionOption — 交互选项（参考 Lyra FInteractionOption）
 *
 * 描述一个完整的交互动作。每个可交互目标可返回多个选项。
 *
 * 两种交互触发方式：
 *
 *   方式一（推荐）：InteractionAbilityToGrant
 *     GrantNearbyInteraction 扫描到目标后，自动把此 GA 授予给交互者。
 *     交互者按键激活此 GA，在自己的 ASC 上执行交互逻辑。
 *     示例：武器拾取 GA → 激活后把武器装入背包。
 *
 *   方式二：TargetAbilitySystem + TargetInteractionAbilityHandle
 *     直接在目标身上激活一个已有的 GA（目标的 ASC 执行）。
 *     示例：门上的 OpenDoor 能力被交互者触发。
 *
 * 示例：
 *   武器拾取：Text="拾取手枪", InteractionAbilityToGrant=BP_GA_PickupPistol
 *   开关门：  Text="开门",     TargetAbilitySystem=门ASC, TargetInteractionAbilityHandle=OpenDoor
 */
USTRUCT(BlueprintType)
struct FDark_TdoreInteractionOption
{
	GENERATED_BODY()

	// ===== 归属信息 =====

	/** 交互目标引用（由 FInteractionOptionBuilder 自动填充） */
	UPROPERTY(BlueprintReadWrite)
	TScriptInterface<IDark_TdoreInteractableTarget> InteractableTarget;

	// ===== 显示文本 =====

	/** 交互提示文本（如 "拾取武器"） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText Text;

	/** 交互副文本（如 "需要 5 点力量"） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText SubText;

	// ===== 方式一：给交互者授予 GA =====

	/** 授予给交互者的 GA 类（GrantNearbyInteraction 扫描后自动授予） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSubclassOf<UGameplayAbility> InteractionAbilityToGrant;

	// ===== 方式二：在目标上激活 GA =====

	/** 目标自身的 ASC（如果要在目标上触发能力） */
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UAbilitySystemComponent> TargetAbilitySystem = nullptr;

	/** 目标上要激活的能力 Handle */
	UPROPERTY(BlueprintReadOnly)
	FGameplayAbilitySpecHandle TargetInteractionAbilityHandle;

	// ===== UI =====

	/** 交互提示 Widget 类（显示在屏幕上的提示图标/文字） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftClassPtr<UUserWidget> InteractionWidgetClass;

	FORCEINLINE bool operator==(const FDark_TdoreInteractionOption& Other) const
	{
		return InteractableTarget == Other.InteractableTarget &&
			InteractionAbilityToGrant == Other.InteractionAbilityToGrant &&
			TargetAbilitySystem == Other.TargetAbilitySystem &&
			TargetInteractionAbilityHandle == Other.TargetInteractionAbilityHandle &&
			InteractionWidgetClass == Other.InteractionWidgetClass &&
			Text.IdenticalTo(Other.Text) && SubText.IdenticalTo(Other.SubText);
	}

	FORCEINLINE bool operator!=(const FDark_TdoreInteractionOption& Other) const { return !operator==(Other); }
	FORCEINLINE bool operator<(const FDark_TdoreInteractionOption& Other) const { return InteractableTarget.GetInterface() < Other.InteractableTarget.GetInterface(); }
};
