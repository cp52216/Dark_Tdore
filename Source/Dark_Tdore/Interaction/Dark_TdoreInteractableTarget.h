// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Dark_TdoreInteractionOption.h"
#include "Dark_TdoreInteractableTarget.generated.h"

struct FDark_TdoreInteractionQuery;

/**
 * FInteractionOptionBuilder — 交互选项构建器（参考 Lyra FInteractionOptionBuilder）
 *
 * 每个可交互目标在 GatherInteractionOptions 中被传入此构建器，
 * 通过 AddInteractionOption 注册自己的交互选项。
 *
 * 内部自动将 Option.InteractableTarget 设置为当前 Scope（即调用方自身）。
 * 这样交互选项总知道是哪个目标提供的。
 */
class FInteractionOptionBuilder
{
public:
	FInteractionOptionBuilder(TScriptInterface<IDark_TdoreInteractableTarget> InterfaceTargetScope,
		TArray<FDark_TdoreInteractionOption>& InteractOptions)
		: Scope(InterfaceTargetScope), Options(InteractOptions) {}

	/** 添加一个交互选项（自动记录目标引用） */
	void AddInteractionOption(const FDark_TdoreInteractionOption& Option)
	{
		FDark_TdoreInteractionOption& OptionEntry = Options.Add_GetRef(Option);
		OptionEntry.InteractableTarget = Scope;
	}

private:
	TScriptInterface<IDark_TdoreInteractableTarget> Scope;   // 当前交互目标
	TArray<FDark_TdoreInteractionOption>& Options;             // 选项输出列表
};

/**
 * IDark_TdoreInteractableTarget — 可交互目标接口（参考 Lyra IInteractableTarget）
 *
 * 任何实现此接口的 Actor 或 Component 都可被交互系统检测到。
 *
 * 典型实现者：
 *   - 拾取物 Actor（武器、血包）
 *   - 门 Actor（可开关）
 *   - NPC 对话组件
 *
 * 工作流程：
 *   GrantNearbyInteraction::QueryInteractables → 球形扫描 → 找到实现了此接口的 Actor
 *     → GatherInteractionOptions → 返回交互选项列表
 *     → 系统根据选项授予交互 GA 给玩家
 */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UDark_TdoreInteractableTarget : public UInterface { GENERATED_BODY() };

class IDark_TdoreInteractableTarget
{
	GENERATED_BODY()

public:
	/**
	 * 收集交互选项
	 * @param InteractQuery 交互查询参数（谁在查询、额外数据）
	 * @param OptionBuilder 通过此构建器添加交互选项
	 *
	 * 示例实现：
	 *   FDark_TdoreInteractionOption Option;
	 *   Option.Text = LOCTEXT("", "拾取武器");
	 *   Option.InteractionAbilityToGrant = UGA_Pickup::StaticClass();
	 *   OptionBuilder.AddInteractionOption(Option);
	 */
	virtual void GatherInteractionOptions(const FDark_TdoreInteractionQuery& InteractQuery,
		FInteractionOptionBuilder& OptionBuilder) = 0;
};
