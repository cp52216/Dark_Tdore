// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreTeamAgentInterface.h"
#include "Dark_TdoreLogChannels.h"
#include "UObject/ScriptInterface.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreTeamAgentInterface)

// ============ UDark_TdoreTeamAgentInterface 构造 ============

UDark_TdoreTeamAgentInterface::UDark_TdoreTeamAgentInterface(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// ============ ConditionalBroadcastTeamChanged ============
// 核心逻辑：仅当 TeamID 确实发生变化时才广播委托
// 调用时机：PlayerState::SetGenericTeamId() / OnRep_MyTeamID() 内

void IDark_TdoreTeamAgentInterface::ConditionalBroadcastTeamChanged(
	TScriptInterface<IDark_TdoreTeamAgentInterface> This,
	FGenericTeamId OldTeamID,
	FGenericTeamId NewTeamID)
{
	if (OldTeamID != NewTeamID)
	{
		const int32 OldTeamIndex = GenericTeamIdToInteger(OldTeamID);
		const int32 NewTeamIndex = GenericTeamIdToInteger(NewTeamID);

		UObject* ThisObj = This.GetObject();
		UE_LOG(LogDark_TdoreGAS, Log, TEXT("[TeamAgent] %s 阵营变更: %d → %d"),
			*GetPathNameSafe(ThisObj), OldTeamIndex, NewTeamIndex);

		// 通过接口获取实现者的委托，触发广播
		This.GetInterface()->GetTeamChangedDelegateChecked().Broadcast(ThisObj, OldTeamIndex, NewTeamIndex);
	}
}
