// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/AnimNotifyState_DarkTdoreInputBufferWindow.h"

#include "Combat/Dark_TdoreCombatInputBufferComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNotifyState_DarkTdoreInputBufferWindow)

void UAnimNotifyState_DarkTdoreInputBufferWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	APawn* Pawn = MeshComp ? Cast<APawn>(MeshComp->GetOwner()) : nullptr;
	if (UDark_TdoreCombatInputBufferComponent* BufferComponent = Pawn ? Pawn->FindComponentByClass<UDark_TdoreCombatInputBufferComponent>() : nullptr)
	{
		BufferComponent->OpenInputBufferWindow(WindowName);
	}
}

void UAnimNotifyState_DarkTdoreInputBufferWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	APawn* Pawn = MeshComp ? Cast<APawn>(MeshComp->GetOwner()) : nullptr;
	if (UDark_TdoreCombatInputBufferComponent* BufferComponent = Pawn ? Pawn->FindComponentByClass<UDark_TdoreCombatInputBufferComponent>() : nullptr)
	{
		BufferComponent->CloseInputBufferWindow(WindowName);
	}
}

