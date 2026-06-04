// Copyright Epic Games, Inc. All Rights Reserved.

#include "Equipment/Dark_TdoreEquipmentManagerComponent.h"

#include "AbilitySystem/Dark_TdoreAbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/ActorChannel.h"
#include "Equipment/Dark_TdoreEquipmentDefinition.h"
#include "Equipment/Dark_TdoreEquipmentInstance.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreEquipmentManagerComponent)

FString FDark_TdoreAppliedEquipmentEntry::GetDebugString() const
{
	return FString::Printf(TEXT("%s of %s"), *GetNameSafe(Instance), *GetNameSafe(EquipmentDefinition.Get()));
}

void FDark_TdoreEquipmentList::PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize)
{
	// 客户端收到装备条目被移除时会走这里。
	// 服务器主动卸下时也会直接调用 OnUnequipped；客户端通过 FastArray 回调同步表现。
	for (const int32 Index : RemovedIndices)
	{
		const FDark_TdoreAppliedEquipmentEntry& Entry = Entries[Index];
		if (Entry.Instance)
		{
			Entry.Instance->OnUnequipped();
		}
	}
}

void FDark_TdoreEquipmentList::PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize)
{
	// 客户端收到新增装备条目时调用装备回调。
	// 这让装备实例的蓝图 OnEquipped 可以在客户端做动画层、特效、UI 等表现。
	for (const int32 Index : AddedIndices)
	{
		const FDark_TdoreAppliedEquipmentEntry& Entry = Entries[Index];
		if (Entry.Instance)
		{
			Entry.Instance->OnEquipped();
		}
	}
}

void FDark_TdoreEquipmentList::PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize)
{
	// 第一阶段没有条目内部字段变化需要处理。
	// 后续如果给装备条目增加耐久、弹药、强化等级，可在这里刷新本地缓存或广播消息。
}

UDark_TdoreAbilitySystemComponent* FDark_TdoreEquipmentList::GetAbilitySystemComponent() const
{
	// Dark_Tdore 的 ASC 挂在 PlayerState，但 Character 实现了 IAbilitySystemInterface。
	// UAbilitySystemGlobals 会沿接口拿到正确的 ASC。
	check(OwnerComponent);
	AActor* OwningActor = OwnerComponent->GetOwner();
	return Cast<UDark_TdoreAbilitySystemComponent>(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwningActor));
}

UDark_TdoreEquipmentInstance* FDark_TdoreEquipmentList::AddEntry(TSubclassOf<UDark_TdoreEquipmentDefinition> EquipmentDefinition)
{
	// 装备列表只允许服务器修改；客户端通过复制接收结果。
	check(EquipmentDefinition != nullptr);
	check(OwnerComponent);
	check(OwnerComponent->GetOwner()->HasAuthority());

	// EquipmentDefinition 是配置，实例类型为空时回退到基础 EquipmentInstance。
	const UDark_TdoreEquipmentDefinition* EquipmentCDO = GetDefault<UDark_TdoreEquipmentDefinition>(EquipmentDefinition);
	TSubclassOf<UDark_TdoreEquipmentInstance> InstanceType = EquipmentCDO->InstanceType;
	if (!InstanceType)
	{
		InstanceType = UDark_TdoreEquipmentInstance::StaticClass();
	}

	// 使用 Pawn/OwnerActor 作为 Outer，便于装备实例 GetPawn/GetWorld，并跟随角色生命周期。
	FDark_TdoreAppliedEquipmentEntry& NewEntry = Entries.AddDefaulted_GetRef();
	NewEntry.EquipmentDefinition = EquipmentDefinition;
	NewEntry.Instance = NewObject<UDark_TdoreEquipmentInstance>(OwnerComponent->GetOwner(), InstanceType);

	UDark_TdoreEquipmentInstance* Result = NewEntry.Instance;
	Result->SetInstigator(OwnerComponent->GetOwner());

	// 装备授予的 AbilitySet 是临时能力：句柄记录到 Entry，卸下时精确回收。
	if (UDark_TdoreAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		for (const TObjectPtr<const UDark_TdoreAbilitySet>& AbilitySet : EquipmentCDO->AbilitySetsToGrant)
		{
			if (AbilitySet)
			{
				AbilitySet->GiveToAbilitySystem(ASC, &NewEntry.GrantedHandles, Result);
			}
		}
	}

	// 生成并挂接装备可见 Actor，例如剑模型。
	Result->SpawnEquipmentActors(EquipmentCDO->ActorsToSpawn);
	// FastArray 条目修改后必须标记脏，网络复制才会同步到客户端。
	MarkItemDirty(NewEntry);

	return Result;
}

void FDark_TdoreEquipmentList::RemoveEntry(UDark_TdoreEquipmentInstance* Instance)
{
	// 通过实例反查装备条目，确保只卸下那一件装备。
	for (auto EntryIt = Entries.CreateIterator(); EntryIt; ++EntryIt)
	{
		FDark_TdoreAppliedEquipmentEntry& Entry = *EntryIt;
		if (Entry.Instance == Instance)
		{
			// 回收装备授予的技能和 GameplayEffect，避免卸下武器后仍能使用武器技能。
			if (UDark_TdoreAbilitySystemComponent* ASC = GetAbilitySystemComponent())
			{
				Entry.GrantedHandles.TakeFromAbilitySystem(ASC);
			}

			// 销毁由该装备生成的所有挂件 Actor。
			Instance->DestroyEquipmentActors();
			EntryIt.RemoveCurrent();
			// 数组结构变化后标记脏，让客户端删除对应装备条目。
			MarkArrayDirty();
			return;
		}
	}
}

UDark_TdoreEquipmentManagerComponent::UDark_TdoreEquipmentManagerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, EquipmentList(this)
{
	SetIsReplicatedByDefault(true);
	// 让组件走 InitializeComponent/UninitializeComponent，便于清理装备。
	bWantsInitializeComponent = true;
}

void UDark_TdoreEquipmentManagerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ThisClass, EquipmentList);
}

UDark_TdoreEquipmentInstance* UDark_TdoreEquipmentManagerComponent::EquipItem(TSubclassOf<UDark_TdoreEquipmentDefinition> EquipmentDefinition)
{
	UDark_TdoreEquipmentInstance* Result = nullptr;
	if (EquipmentDefinition)
	{
		// 添加条目会创建实例、授予能力、生成挂件 Actor。
		Result = EquipmentList.AddEntry(EquipmentDefinition);
		if (Result)
		{
			// 服务器本地立即触发装备回调；客户端通过 FastArray PostReplicatedAdd 触发。
			Result->OnEquipped();
			if (IsUsingRegisteredSubObjectList() && IsReadyForReplication())
			{
				// UE5 注册子对象复制路径：让装备 UObject 自身也参与复制。
				AddReplicatedSubObject(Result);
			}
		}
	}
	return Result;
}

void UDark_TdoreEquipmentManagerComponent::UnequipItem(UDark_TdoreEquipmentInstance* ItemInstance)
{
	if (!ItemInstance)
	{
		return;
	}

	if (IsUsingRegisteredSubObjectList())
	{
		// 先从复制子对象列表移除，再清理装备条目。
		RemoveReplicatedSubObject(ItemInstance);
	}

	// 服务器本地立即触发卸下回调；客户端通过 FastArray PreReplicatedRemove 触发。
	ItemInstance->OnUnequipped();
	EquipmentList.RemoveEntry(ItemInstance);
}

bool UDark_TdoreEquipmentManagerComponent::ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch, FReplicationFlags* RepFlags)
{
	bool WroteSomething = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);

	// 兼容传统 ActorChannel 子对象复制路径，确保装备实例 UObject 能同步到客户端。
	for (FDark_TdoreAppliedEquipmentEntry& Entry : EquipmentList.Entries)
	{
		if (IsValid(Entry.Instance))
		{
			WroteSomething |= Channel->ReplicateSubobject(Entry.Instance, *Bunch, *RepFlags);
		}
	}

	return WroteSomething;
}

void UDark_TdoreEquipmentManagerComponent::InitializeComponent()
{
	Super::InitializeComponent();
}

void UDark_TdoreEquipmentManagerComponent::UninitializeComponent()
{
	// 组件销毁前先复制一份列表，避免 UnequipItem 修改数组时迭代器失效。
	TArray<UDark_TdoreEquipmentInstance*> AllEquipmentInstances;
	for (const FDark_TdoreAppliedEquipmentEntry& Entry : EquipmentList.Entries)
	{
		AllEquipmentInstances.Add(Entry.Instance);
	}

	for (UDark_TdoreEquipmentInstance* EquipInstance : AllEquipmentInstances)
	{
		UnequipItem(EquipInstance);
	}

	Super::UninitializeComponent();
}

void UDark_TdoreEquipmentManagerComponent::ReadyForReplication()
{
	Super::ReadyForReplication();

	if (IsUsingRegisteredSubObjectList())
	{
		// 如果装备在组件 ReadyForReplication 前已经存在，这里补注册子对象。
		for (const FDark_TdoreAppliedEquipmentEntry& Entry : EquipmentList.Entries)
		{
			if (IsValid(Entry.Instance))
			{
				AddReplicatedSubObject(Entry.Instance);
			}
		}
	}
}

UDark_TdoreEquipmentInstance* UDark_TdoreEquipmentManagerComponent::GetFirstInstanceOfType(TSubclassOf<UDark_TdoreEquipmentInstance> InstanceType)
{
	for (FDark_TdoreAppliedEquipmentEntry& Entry : EquipmentList.Entries)
	{
		if (Entry.Instance && Entry.Instance->IsA(InstanceType))
		{
			return Entry.Instance;
		}
	}

	return nullptr;
}

TArray<UDark_TdoreEquipmentInstance*> UDark_TdoreEquipmentManagerComponent::GetEquipmentInstancesOfType(TSubclassOf<UDark_TdoreEquipmentInstance> InstanceType) const
{
	TArray<UDark_TdoreEquipmentInstance*> Results;
	for (const FDark_TdoreAppliedEquipmentEntry& Entry : EquipmentList.Entries)
	{
		if (Entry.Instance && Entry.Instance->IsA(InstanceType))
		{
			Results.Add(Entry.Instance);
		}
	}
	return Results;
}

UDark_TdoreEquipmentInstance* UDark_TdoreEquipmentManagerComponent::GetFirstInstanceOfDefinition(TSubclassOf<UDark_TdoreEquipmentDefinition> EquipmentDefinition) const
{
	if (!EquipmentDefinition)
	{
		return nullptr;
	}

	// EquipmentDefinition 是一件装备的数据身份。用定义类比较，比用实例类更精确：
	// 两把不同剑可以共用同一个 WeaponInstance 类型，但仍然是不同装备定义。
	for (const FDark_TdoreAppliedEquipmentEntry& Entry : EquipmentList.Entries)
	{
		if (Entry.EquipmentDefinition == EquipmentDefinition)
		{
			return Entry.Instance;
		}
	}

	return nullptr;
}

void UDark_TdoreEquipmentManagerComponent::UnequipAllItemsOfType(TSubclassOf<UDark_TdoreEquipmentInstance> InstanceType)
{
	if (!InstanceType)
	{
		return;
	}

	// UnequipItem 会修改 FastArray，所以先复制一份目标列表，避免遍历时容器失效。
	TArray<UDark_TdoreEquipmentInstance*> InstancesToUnequip;
	for (const FDark_TdoreAppliedEquipmentEntry& Entry : EquipmentList.Entries)
	{
		if (Entry.Instance && Entry.Instance->IsA(InstanceType))
		{
			InstancesToUnequip.Add(Entry.Instance);
		}
	}

	for (UDark_TdoreEquipmentInstance* Instance : InstancesToUnequip)
	{
		UnequipItem(Instance);
	}
}
