// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenericTeamAgentInterface.h"
#include "Dark_TdoreTeamAgentInterface.generated.h"

class UObject;

// ============ 阵营变更委托 ============

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnDarkTdoreTeamIndexChangedDelegate,
	UObject*, ObjectChangingTeam,  // 阵营变更的对象
	int32,   OldTeamID,            // 旧阵营 ID
	int32,   NewTeamID);           // 新阵营 ID

// ============ 工具函数：GenericTeamId ↔ int32 互转 ============

/** GenericTeamId → int32（NoTeam → INDEX_NONE） */
inline int32 GenericTeamIdToInteger(FGenericTeamId ID)
{
	return (ID == FGenericTeamId::NoTeam) ? INDEX_NONE : (int32)ID;
}

/** int32 → GenericTeamId（INDEX_NONE → NoTeam） */
inline FGenericTeamId IntegerToGenericTeamId(int32 ID)
{
	return (ID == INDEX_NONE) ? FGenericTeamId::NoTeam : FGenericTeamId((uint8)ID);
}

// ============ 阵营代理接口 ============

/**
 * IDark_TdoreTeamAgentInterface — 阵营代理接口（参考 Lyra ILyraTeamAgentInterface）
 *
 * 任何需要「属于某个阵营」的对象（PlayerState、Pawn）都应实现此接口。
 *
 * 继承自 UE 引擎的 UGenericTeamAgentInterface，在此基础上扩展：
 *   - 阵营变更委托广播（ConditionalBroadcastTeamChanged）
 *   - 委托获取（GetOnTeamIndexChangedDelegate）
 *
 * 注意：meta=(CannotImplementInterfaceInBlueprint) — 仅 C++ 实现
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UDark_TdoreTeamAgentInterface : public UGenericTeamAgentInterface
{
	GENERATED_UINTERFACE_BODY()
};

class IDark_TdoreTeamAgentInterface : public IGenericTeamAgentInterface
{
	GENERATED_IINTERFACE_BODY()

	/** 返回阵营变更委托（默认 nullptr，子类覆写返回 &OnTeamChangedDelegate） */
	virtual FOnDarkTdoreTeamIndexChangedDelegate* GetOnTeamIndexChangedDelegate() { return nullptr; }

	/**
	 * 阵营变更条件广播 — 仅当新旧阵营不同时才触发广播
	 * @param This      调用者自身（TScriptInterface）
	 * @param OldTeamID 旧阵营 ID
	 * @param NewTeamID 新阵营 ID
	 */
	static void ConditionalBroadcastTeamChanged(
		TScriptInterface<IDark_TdoreTeamAgentInterface> This,
		FGenericTeamId OldTeamID,
		FGenericTeamId NewTeamID);

	/** 获取委托（带 check 保证非空） */
	FOnDarkTdoreTeamIndexChangedDelegate& GetTeamChangedDelegateChecked()
	{
		FOnDarkTdoreTeamIndexChangedDelegate* Result = GetOnTeamIndexChangedDelegate();
		check(Result);
		return *Result;
	}
};
