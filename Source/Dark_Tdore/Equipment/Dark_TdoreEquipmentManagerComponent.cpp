// Copyright Epic Games, Inc. All Rights Reserved.

#include "Equipment/Dark_TdoreEquipmentManagerComponent.h"

#include "AbilitySystem/Dark_TdoreAbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/ActorChannel.h"
#include "Equipment/Dark_TdoreEquipmentDefinition.h"
#include "Equipment/Dark_TdoreEquipmentInstance.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreEquipmentManagerComponent)

// ============================================================================
// FDark_TdoreAppliedEquipmentEntry — 单个装备条目
// ============================================================================
// 这是 FastArrayReplication 的基本单元。
// 每个条目代表角色身上一件已装备的东西（一把剑、一个盾等）。
// EquipmentList.Entries 数组通过网络复制同步到客户端。
//

FString FDark_TdoreAppliedEquipmentEntry::GetDebugString() const
{
	return FString::Printf(TEXT("%s of %s"), *GetNameSafe(Instance), *GetNameSafe(EquipmentDefinition.Get()));
}

// ============================================================================
// FDark_TdoreEquipmentList — 装备列表（FastArray）
// ============================================================================
// 这是 UE FFastArraySerializer 的派生结构。
// FastArray 提供增量的网络复制：只同步增删改的条目，不每次全量传输。
//
// 三个 FastArray 回调：
//   PreReplicatedRemove — 客户端收到"装备条目被删了"
//   PostReplicatedAdd   — 客户端收到"新增了装备条目"
//   PostReplicatedChange — 客户端收到"某个装备条目内部字段变了"
//

void FDark_TdoreEquipmentList::PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize)
{
	// 客户端收到装备条目被移除时会走这里。
	// 服务器主动卸下时，服务器自己已经调过 OnUnequipped；
	// 客户端通过这个 FastArray 回调同步表现（卸载动画层、隐藏武器等）。
	for (const int32 Index : RemovedIndices)
	{
		const FDark_TdoreAppliedEquipmentEntry& Entry = Entries[Index];
		if (Entry.Instance)
		{
			// 客户端的 OnUnequipped 只做表现层清理（动画层、特效、UI）
			// 不涉及能力回收——能力回收在服务器的 RemoveEntry 里做
			Entry.Instance->OnUnequipped();
		}
	}
}

void FDark_TdoreEquipmentList::PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize)
{
	// 客户端收到新增装备条目时调用 OnEquipped。
	// 例如：其他玩家装备了剑，这里让本机客户端播放拔刀动画、显示武器模型。
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
	// 后续如果给装备条目增加耐久、弹药、强化等级，
	// 可在这里刷新本地缓存或广播消息。
}

// ============================================================================
// GetAbilitySystemComponent — 获取 ASC
// ============================================================================
// 根据项目架构，ASC 挂在 PlayerState 上。
// Character 实现了 IAbilitySystemInterface，把查询委托到 PlayerState。
// UAbilitySystemGlobals::GetAbilitySystemComponentFromActor 沿接口拿到正确的 ASC。
//
UDark_TdoreAbilitySystemComponent* FDark_TdoreEquipmentList::GetAbilitySystemComponent() const
{
	check(OwnerComponent);
	AActor* OwningActor = OwnerComponent->GetOwner();
	return Cast<UDark_TdoreAbilitySystemComponent>(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwningActor));
}

// ============================================================================
// AddEntry — 添加装备条目（仅服务器）
// ============================================================================
// 完整流程（服务器执行）：
//   1. 从 EquipmentDefinition 配置中读取实例类型（如 WeaponInstance）
//   2. 用 NewObject 创建装备实例（Outer 是 OwnerActor，保证生命周期一致）
//   3. 设置 Instigator
//   4. 授予装备定义里的 AbilitySet（如武器的技能集）
//       → 句柄记录到 GrantedHandles，卸下时用于精确回收
//   5. 生成装备的可见 Actor（如剑的 StaticMesh）
//       → SpawnEquipmentActors 根据配置的 ActorsToSpawn 生成
//   6. MarkItemDirty → 标记 FastArray 条目脏，触发网络复制到客户端
//
// 返回值：新创建的装备实例，供调用方（EquipItem）进一步处理
//
UDark_TdoreEquipmentInstance* FDark_TdoreEquipmentList::AddEntry(TSubclassOf<UDark_TdoreEquipmentDefinition> EquipmentDefinition)
{
	// 装备列表只允许服务器修改；客户端通过 FastArray 复制接收结果。
	check(EquipmentDefinition != nullptr);
	check(OwnerComponent);
	check(OwnerComponent->GetOwner()->HasAuthority());

	// EquipmentDefinition 是蓝图配置资产（DataAsset）。
	// 取 CDO 读取 InstanceType：如果配置为空，回退到基础 EquipmentInstance。
	const UDark_TdoreEquipmentDefinition* EquipmentCDO = GetDefault<UDark_TdoreEquipmentDefinition>(EquipmentDefinition);
	TSubclassOf<UDark_TdoreEquipmentInstance> InstanceType = EquipmentCDO->InstanceType;
	if (!InstanceType)
	{
		InstanceType = UDark_TdoreEquipmentInstance::StaticClass();
	}

	// NewObject 以 OwnerActor（Character/Pawn）作为 Outer
	// 好处：
	//   - 装备实例跟随角色生命周期（角色销毁 → 装备实例被 GC）
	//   - GetPawn() / GetWorld() 可以通过 Outer 链正常工作
	FDark_TdoreAppliedEquipmentEntry& NewEntry = Entries.AddDefaulted_GetRef();
	NewEntry.EquipmentDefinition = EquipmentDefinition;
	NewEntry.Instance = NewObject<UDark_TdoreEquipmentInstance>(OwnerComponent->GetOwner(), InstanceType);

	UDark_TdoreEquipmentInstance* Result = NewEntry.Instance;
	// Instigator 记录"是谁让这件装备被装备的"（通常是角色自己）
	// 用于装备能力的 SourceObject 追踪
	Result->SetInstigator(OwnerComponent->GetOwner());

	// 装备定义里可以有零到多个 AbilitySet（技能 + GE 的捆绑 DataAsset）
	// 例如：剑的 AbilitySet 里包含 GA_MeleeCombo_Sword_Light 等技能
	// GiveToAbilitySystem 会把技能授予到 ASC，句柄记录到 GrantedHandles
	// 卸下时通过 TakeFromAbilitySystem 精确移除这些技能
	if (UDark_TdoreAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		for (const TObjectPtr<const UDark_TdoreAbilitySet>& AbilitySet : EquipmentCDO->AbilitySetsToGrant)
		{
			if (AbilitySet)
			{
				// Result 作为 SourceObject 传入，技能知道"是这件装备给我的"
				AbilitySet->GiveToAbilitySystem(ASC, &NewEntry.GrantedHandles, Result);
			}
		}
	}

	// 生成装备的可见 Actor（武器模型等）
	// EquipmentCDO->ActorsToSpawn 是 TSubclassOf<AActor> 数组
	// SpawnEquipmentActors 内部会 SpawnActor + AttachToComponent + Initialize
	Result->SpawnEquipmentActors(EquipmentCDO->ActorsToSpawn);

	// FastArray 条目标记脏 → 触发 PreReplicatedAdd → 逐个复制字段变化
	MarkItemDirty(NewEntry);

	return Result;
}

// ============================================================================
// RemoveEntry — 移除装备条目（仅服务器）
// ============================================================================
// 完整流程（服务器执行）：
//   1. 反查装备条目（用实例指针匹配）
//   2. 回收装备授予的技能和 GE（TakeFromAbilitySystem）
//   3. 销毁装备生成的所有可见 Actor（DestroyEquipmentActors）
//   4. 从数组中移除该条目 → MarkArrayDirty → 触发 PreReplicatedRemove
//
void FDark_TdoreEquipmentList::RemoveEntry(UDark_TdoreEquipmentInstance* Instance)
{
	// 遍历找匹配的实例——用指针相等比较，不是类匹配
	for (auto EntryIt = Entries.CreateIterator(); EntryIt; ++EntryIt)
	{
		FDark_TdoreAppliedEquipmentEntry& Entry = *EntryIt;
		if (Entry.Instance == Instance)
		{
			// 回收技能：ClearAbility 每一个曾授予的技能
			// 回收 GE：RemoveActiveGameplayEffect 每一个曾应用的 GE
			// 如果没有这一步，卸下剑后玩家仍能按攻击键释放剑技能
			if (UDark_TdoreAbilitySystemComponent* ASC = GetAbilitySystemComponent())
			{
				Entry.GrantedHandles.TakeFromAbilitySystem(ASC);
			}

			// 销毁可见 Actor：DestroyEquipmentActors 会 Destroy() 所有 SpawnEquipmentActors 生成的 Actor
			Instance->DestroyEquipmentActors();

			// 从数组中删除当前条目
			EntryIt.RemoveCurrent();

			// 数组结构变化（删除元素） → 标记脏
			// FastArray 会把删除事件发送给客户端 → PreReplicatedRemove
			MarkArrayDirty();
			return;
		}
	}
}

// ============================================================================
// 构造 — UDark_TdoreEquipmentManagerComponent
// ============================================================================
// 挂在 Character/Pawn 上，管理该角色的所有装备。
// EquipmentList 通过 FastArray 网络复制，客户端自动同步全部装备状态。
//
UDark_TdoreEquipmentManagerComponent::UDark_TdoreEquipmentManagerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	// EquipmentList 需要 OwnerComponent 指针来取 ASC、Owner 等
	// 这里把 this 传给它，构成"组件 ↔ 列表"的双向引用
	, EquipmentList(this)
{
	// 组件本身需要网络复制（EquipmentList 作为复制属性会被同步）
	SetIsReplicatedByDefault(true);

	// 让组件走 InitializeComponent / UninitializeComponent
	// 在 UninitializeComponent 里自动卸下所有装备，防止残留
	bWantsInitializeComponent = true;
}

// ============================================================================
// GetLifetimeReplicatedProps — 声明哪些属性需要网络复制
// ============================================================================
// 只需复制 EquipmentList 这一个属性。
// FastArray 机制会自动处理增量同步（只发变化的条目）。
//
void UDark_TdoreEquipmentManagerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ThisClass, EquipmentList);
}

// ============================================================================
// EquipItem — 装备一件物品（公开 API）
// ============================================================================
// 这是蓝图/其他系统调用的入口。
// 流程：
//   1. AddEntry → 创建实例、授予能力、生成 Actor（详细见 AddEntry 注释）
//   2. OnEquipped → 服务器本地立即触发装备回调
//   3. 如果 UE5 子对象复制开启 → 注册装备 UObject 参与复制
//
// 注意：客户端不会直接调这个函数（有 HasAuthority 检查）。
// 客户端通过 FastArray 的 PostReplicatedAdd 感知装备。
//
UDark_TdoreEquipmentInstance* UDark_TdoreEquipmentManagerComponent::EquipItem(TSubclassOf<UDark_TdoreEquipmentDefinition> EquipmentDefinition)
{
	UDark_TdoreEquipmentInstance* Result = nullptr;
	if (EquipmentDefinition)
	{
		// AddEntry → 创建实例 + 授予 AbilitySet + 生成 Actor
		Result = EquipmentList.AddEntry(EquipmentDefinition);
		if (Result)
		{
			// 服务器本地立即调 OnEquipped
			// 这会触发：设置装备 Tag → 挂载动画层 → 播放拔刀 Montage 等
			Result->OnEquipped();

			// UE5 的 RegisteredSubObjectList 复制路径：
			// 如果开启，装备 UObject 本身（不仅是 FastArray 条目）也会参与复制
			// 传统路径走 ReplicateSubobjects，新路径走 AddReplicatedSubObject
			if (IsUsingRegisteredSubObjectList() && IsReadyForReplication())
			{
				AddReplicatedSubObject(Result);
			}
		}
	}
	return Result;
}

// ============================================================================
// UnequipItem — 卸下一件装备（公开 API）
// ============================================================================
// 流程：
//   1. 如果 UE5 子对象复制开启 → 先取消注册
//   2. OnUnequipped → 服务器本地触发卸下回调
//   3. RemoveEntry → 回收技能、销毁 Actor、移除条目
//
void UDark_TdoreEquipmentManagerComponent::UnequipItem(UDark_TdoreEquipmentInstance* ItemInstance)
{
	if (!ItemInstance)
	{
		return;
	}

	if (IsUsingRegisteredSubObjectList())
	{
		// 和 EquipItem 的 AddReplicatedSubObject 对应，取消子对象复制
		RemoveReplicatedSubObject(ItemInstance);
	}

	// 服务器本地触发卸下：移除装备 Tag → 卸载动画层 → 播放收刀 Montage
	ItemInstance->OnUnequipped();

	// RemoveEntry 里做：回收技能 + GE → 销毁 Actor → 条目删除
	EquipmentList.RemoveEntry(ItemInstance);
}

// ============================================================================
// ReplicateSubobjects — 传统 ActorChannel 子对象复制
// ============================================================================
// 兼容没有开启 RegisteredSubObjectList 的情况。
// Channel->ReplicateSubobject 会把装备 UObject 序列化到网络包中。
//
bool UDark_TdoreEquipmentManagerComponent::ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch, FReplicationFlags* RepFlags)
{
	bool WroteSomething = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);

	for (FDark_TdoreAppliedEquipmentEntry& Entry : EquipmentList.Entries)
	{
		if (IsValid(Entry.Instance))
		{
			// 让每个装备实例也参与网络复制
			// 这样客户端的装备实例会有正确的状态（非 CDO 快照）
			WroteSomething |= Channel->ReplicateSubobject(Entry.Instance, *Bunch, *RepFlags);
		}
	}

	return WroteSomething;
}

void UDark_TdoreEquipmentManagerComponent::InitializeComponent()
{
	Super::InitializeComponent();
}

// ============================================================================
// UninitializeComponent — 组件销毁前清理所有装备
// ============================================================================
// 重要：必须先复制一份装备列表再遍历卸载。
// 因为 UnequipItem 会修改 EquipmentList.Entries 数组，
// 直接在 Entries 上遍历会导致迭代器失效。
//
void UDark_TdoreEquipmentManagerComponent::UninitializeComponent()
{
	// 第一步：复制所有装备实例到临时数组
	// 不能用 for (auto& Entry : EquipmentList.Entries) 因为 UnequipItem 会修改数组
	TArray<UDark_TdoreEquipmentInstance*> AllEquipmentInstances;
	for (const FDark_TdoreAppliedEquipmentEntry& Entry : EquipmentList.Entries)
	{
		AllEquipmentInstances.Add(Entry.Instance);
	}

	// 第二步：逐个卸下
	// UnequipItem 做三件事：取消复制 → OnUnequipped 回调 → RemoveEntry（回收技能+销毁Actor）
	for (UDark_TdoreEquipmentInstance* EquipInstance : AllEquipmentInstances)
	{
		UnequipItem(EquipInstance);
	}

	Super::UninitializeComponent();
}

// ============================================================================
// ReadyForReplication — 网络复制就绪回调
// ============================================================================
// 如果装备在组件 ReadyForReplication 之前就已存在（比如构造时就装备了）
// 需要在这里补注册子对象，确保 UE5 子对象复制路径不遗漏。
//
void UDark_TdoreEquipmentManagerComponent::ReadyForReplication()
{
	Super::ReadyForReplication();

	if (IsUsingRegisteredSubObjectList())
	{
		for (const FDark_TdoreAppliedEquipmentEntry& Entry : EquipmentList.Entries)
		{
			if (IsValid(Entry.Instance))
			{
				AddReplicatedSubObject(Entry.Instance);
			}
		}
	}
}

// ============================================================================
// GetFirstInstanceOfType — 按实例类型查找第一件装备
// ============================================================================
// AnimInstance 用它判断"角色是否有 WeaponInstance"。
// 和 GetEquipmentInstancesOfType 的区别：只要第一件就够了。
//
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

// ============================================================================
// GetEquipmentInstancesOfType — 按实例类型查找所有装备
// ============================================================================
// 返回指定类型的所有装备实例。
// 例如：GetEquipmentInstancesOfType(WeaponInstance) 可能返回主武器 + 副武器。
//
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

// ============================================================================
// GetFirstInstanceOfDefinition — 按装备定义查找第一件装备
// ============================================================================
// 和 GetFirstInstanceOfType 的区别：
//  - GetFirstInstanceOfType  按"实例运行时类型"匹配（IsA）
//  - GetFirstInstanceOfDefinition 按"装备定义资产"匹配（指针相等）
//
// 为什么需要这个：
//   两把不同剑可能用同一个 WeaponInstance 子类，
//   但它们的 EquipmentDefinition 是不同的 DataAsset。
//   用 Definition 匹配可以精确区分"火剑"和"冰剑"。
//
UDark_TdoreEquipmentInstance* UDark_TdoreEquipmentManagerComponent::GetFirstInstanceOfDefinition(TSubclassOf<UDark_TdoreEquipmentDefinition> EquipmentDefinition) const
{
	if (!EquipmentDefinition)
	{
		return nullptr;
	}

	for (const FDark_TdoreAppliedEquipmentEntry& Entry : EquipmentList.Entries)
	{
		// EquipmentDefinition 是 TSubclassOf，直接比较类指针
		if (Entry.EquipmentDefinition == EquipmentDefinition)
		{
			return Entry.Instance;
		}
	}

	return nullptr;
}

// ============================================================================
// UnequipAllItemsOfType — 批量卸下某类型的所有装备
// ============================================================================
// 和 UninitializeComponent 一样的处理方式：
// 先复制目标列表 → 再逐个卸下（避免遍历时修改数组）。
//
void UDark_TdoreEquipmentManagerComponent::UnequipAllItemsOfType(TSubclassOf<UDark_TdoreEquipmentInstance> InstanceType)
{
	if (!InstanceType)
	{
		return;
	}

	// 先收集所有匹配的实例到临时数组
	TArray<UDark_TdoreEquipmentInstance*> InstancesToUnequip;
	for (const FDark_TdoreAppliedEquipmentEntry& Entry : EquipmentList.Entries)
	{
		if (Entry.Instance && Entry.Instance->IsA(InstanceType))
		{
			InstancesToUnequip.Add(Entry.Instance);
		}
	}

	// 再逐个卸下
	// UnequipItem 内部修改 EquipmentList，所以必须在拷贝的数组上遍历
	for (UDark_TdoreEquipmentInstance* Instance : InstancesToUnequip)
	{
		UnequipItem(Instance);
	}
}
