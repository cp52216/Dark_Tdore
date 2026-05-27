// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreVerbMessage.h"

// ============ FDark_TdoreVerbMessage::ToString ============
// 导出可读的调试信息

FString FDark_TdoreVerbMessage::ToString() const
{
	FString HumanReadableMessage;
	FDark_TdoreVerbMessage::StaticStruct()->ExportText(
		/*out*/ HumanReadableMessage,
		this,
		/*Defaults=*/ nullptr,
		/*OwnerObject=*/ nullptr,
		PPF_None,
		/*ExportRootScope=*/ nullptr);
	return HumanReadableMessage;
}
