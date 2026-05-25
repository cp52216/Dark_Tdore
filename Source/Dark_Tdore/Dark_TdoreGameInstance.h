// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/GameInstance.h"
#include "Dark_TdoreGameInstance.generated.h"

/**
 * UDark_TdoreGameInstance
 * 注册 InitState 状态链到 GameFrameworkComponentManager（参考 Lyra ULyraGameInstance）
 */
UCLASS()
class UDark_TdoreGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
};
