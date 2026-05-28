// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Dark_TdoreInteractionQuery.generated.h"

/**
 * FDark_TdoreInteractionQuery — 交互查询参数（参考 Lyra FInteractionQuery）
 *
 * 发起交互时传入的上下文信息。
 * 每个可交互目标在 GatherInteractionOptions 中接收此参数，
 * 可根据查询者身份、控制器类型返回不同的交互选项。
 *
 * 示例：
 *   - 只有拥有钥匙的玩家才能看到"开门"选项 → 通过 OptionalObjectData 传入背包组件
 *   - AI 看到不同选项 → 通过 RequestingController 判断是 PlayerController 还是 AIController
 */
USTRUCT(BlueprintType)
struct FDark_TdoreInteractionQuery
{
	GENERATED_BODY()

	/** 发起交互的角色（如玩家的 Pawn） */
	UPROPERTY(BlueprintReadWrite)
	TWeakObjectPtr<AActor> RequestingAvatar;

	/** 发起者的控制器（可区分玩家/AI） */
	UPROPERTY(BlueprintReadWrite)
	TWeakObjectPtr<AController> RequestingController;

	/** 额外上下文数据（如背包组件、任务组件） */
	UPROPERTY(BlueprintReadWrite)
	TWeakObjectPtr<UObject> OptionalObjectData;
};
