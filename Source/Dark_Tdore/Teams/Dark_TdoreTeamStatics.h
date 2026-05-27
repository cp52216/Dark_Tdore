// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Dark_TdoreTeamStatics.generated.h"

class UDark_TdoreTeamDisplayAsset;
class UObject;

/**
 * UDark_TdoreTeamStatics — 阵营静态工具函数（参考 Lyra ULyraTeamStatics）
 *
 * 蓝图可直接调用的快捷函数，内部封装了 TeamSubsystem 的查询。
 *
 * 典型蓝图用法：
 *   FindTeamFromObject(Self, bIsPartOfTeam, TeamId, DisplayAsset);
 *   → 如果 bIsPartOfTeam==true，可以用 DisplayAsset->ApplyToActor 上色
 */
UCLASS()
class UDark_TdoreTeamStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 根据 Agent 对象查找其阵营信息
	 *
	 * @param Agent         要查询的对象（通常为 Self/Pawn/PlayerState）
	 * @param bIsPartOfTeam [out] 是否属于某个阵营
	 * @param TeamId        [out] 阵营 ID（INDEX_NONE 表示无阵营）
	 * @param DisplayAsset  [out] 阵营显示资产（可能为 nullptr）
	 */
	UFUNCTION(BlueprintCallable, Category = Teams, meta = (DefaultToSelf = "Agent"))
	static void FindTeamFromObject(const UObject* Agent, bool& bIsPartOfTeam, int32& TeamId,
		UDark_TdoreTeamDisplayAsset*& DisplayAsset);
};
