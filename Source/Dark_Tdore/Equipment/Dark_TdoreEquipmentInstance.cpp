// Copyright Epic Games, Inc. All Rights Reserved.

#include "Equipment/Dark_TdoreEquipmentInstance.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreEquipmentInstance)

UDark_TdoreEquipmentInstance::UDark_TdoreEquipmentInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UWorld* UDark_TdoreEquipmentInstance::GetWorld() const
{
	// 装备实例本身不是 Actor，不能直接拿 World。
	// 这里通过 Outer Pawn 间接取得 World，保证蓝图和 C++ 都能在实例里安全 Spawn/计时。
	if (const APawn* OwningPawn = GetPawn())
	{
		return OwningPawn->GetWorld();
	}

	return nullptr;
}

void UDark_TdoreEquipmentInstance::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ThisClass, Instigator);
	DOREPLIFETIME(ThisClass, SpawnedActors);
}

APawn* UDark_TdoreEquipmentInstance::GetPawn() const
{
	// 装备管理组件创建实例时使用 Owner Pawn 作为 Outer。
	// 因此这里可以通过 Outer 找回拥有该装备的 Pawn。
	return Cast<APawn>(GetOuter());
}

APawn* UDark_TdoreEquipmentInstance::GetTypedPawn(TSubclassOf<APawn> PawnType) const
{
	// 蓝图常需要拿具体 Pawn 类型。这里做类型检查后再返回，避免蓝图 Cast 节点到处散落。
	APawn* Result = nullptr;
	if (UClass* ActualPawnType = PawnType)
	{
		if (GetOuter() && GetOuter()->IsA(ActualPawnType))
		{
			Result = Cast<APawn>(GetOuter());
		}
	}
	return Result;
}

void UDark_TdoreEquipmentInstance::SpawnEquipmentActors(const TArray<FDark_TdoreEquipmentActorToSpawn>& ActorsToSpawn)
{
	// 装备可见物由 EquipmentDefinition 配置。
	// 对 Character 优先挂到 Mesh，这样 hand_rSocket 等骨骼插槽才能生效；
	// 非 Character Pawn 则退回挂到 RootComponent。
	APawn* OwningPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!OwningPawn || !World)
	{
		return;
	}

	USceneComponent* AttachTarget = OwningPawn->GetRootComponent();
	if (ACharacter* Character = Cast<ACharacter>(OwningPawn))
	{
		AttachTarget = Character->GetMesh();
	}

	for (const FDark_TdoreEquipmentActorToSpawn& SpawnInfo : ActorsToSpawn)
	{
		// 单个挂件配置不完整时跳过，避免装备整件失败。
		if (!SpawnInfo.ActorToSpawn || !AttachTarget)
		{
			continue;
		}

		// Deferred Spawn 方便后续扩展：未来可以在 FinishSpawning 前注入队伍、材质、拥有者等数据。
		AActor* NewActor = World->SpawnActorDeferred<AActor>(SpawnInfo.ActorToSpawn, FTransform::Identity, OwningPawn);
		if (!NewActor)
		{
			continue;
		}

		NewActor->FinishSpawning(FTransform::Identity, true);
		NewActor->SetActorRelativeTransform(SpawnInfo.AttachTransform);
		NewActor->AttachToComponent(AttachTarget, FAttachmentTransformRules::KeepRelativeTransform, SpawnInfo.AttachSocket);
		SpawnedActors.Add(NewActor);
	}
}

void UDark_TdoreEquipmentInstance::DestroyEquipmentActors()
{
	// 只销毁由本装备实例生成的 Actor，不碰角色身上其他组件或外部 Actor。
	for (AActor* Actor : SpawnedActors)
	{
		if (Actor)
		{
			Actor->Destroy();
		}
	}
	SpawnedActors.Reset();
}

void UDark_TdoreEquipmentInstance::OnEquipped()
{
	// C++ 子类可以覆写 OnEquipped；蓝图子类实现 K2_OnEquipped 做表现层扩展。
	K2_OnEquipped();
}

void UDark_TdoreEquipmentInstance::OnUnequipped()
{
	// C++ 子类可以覆写 OnUnequipped；蓝图子类实现 K2_OnUnequipped 做表现层清理。
	K2_OnUnequipped();
}

void UDark_TdoreEquipmentInstance::OnRep_Instigator()
{
}
