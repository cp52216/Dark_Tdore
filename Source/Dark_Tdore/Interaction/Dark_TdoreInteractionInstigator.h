// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Dark_TdoreInteractionOption.h"
#include "Dark_TdoreInteractionInstigator.generated.h"

struct FDark_TdoreInteractionQuery;

/**
 * IDark_TdoreInteractionInstigator — 交互选择仲裁接口（参考 Lyra IInteractionInstigator）
 *
 * 当附近有多个可交互目标时（如同时面对一扇门和一把武器），
 * 实现此接口的对象负责选择「最佳」交互选项。
 *
 * 典型实现者：
 *   - PlayerController（根据准心方向选择最近的目标）
 *   - Pawn（根据面向方向选择）
 */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UDark_TdoreInteractionInstigator : public UInterface { GENERATED_BODY() };

class IDark_TdoreInteractionInstigator
{
	GENERATED_BODY()

public:
	/**
	 * 在多个交互选项中选出最佳的一个
	 * @param InteractQuery   交互查询参数
	 * @param InteractOptions 当前所有可用的交互选项
	 * @return 仲裁后的最佳选项
	 */
	virtual FDark_TdoreInteractionOption ChooseBestInteractionOption(
		const FDark_TdoreInteractionQuery& InteractQuery,
		const TArray<FDark_TdoreInteractionOption>& InteractOptions) = 0;
};
