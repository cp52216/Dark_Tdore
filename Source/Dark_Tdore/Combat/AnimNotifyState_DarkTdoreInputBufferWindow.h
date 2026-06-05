// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_DarkTdoreInputBufferWindow.generated.h"

UCLASS(meta = (DisplayName = "DarkTdore Input Buffer Window"))
class UAnimNotifyState_DarkTdoreInputBufferWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

private:
	// 窗口名用于调试和未来扩展。当前实现只做计数，不按名字区分消费规则。
	UPROPERTY(EditAnywhere, Category = "Input Buffer")
	FName WindowName = TEXT("Combo");
};
