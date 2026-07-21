// Copyright Epic Games, Inc. All Rights Reserved.

#include "Equipment/Dark_TdoreEquipmentInstance.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"
#include "Weapons/Dark_TdoreWeaponActor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreEquipmentInstance)

UDark_TdoreEquipmentInstance::UDark_TdoreEquipmentInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 构造时不需要额外初始化。
	// Instigator 在 AddEntry 里由 SetInstigator 赋值。
	// SpawnedActors 在 SpawnEquipmentActors 里填充。
}

// ============================================================================
// GetWorld — 通过 Outer Pawn 获取 World
// ============================================================================
// 装备实例是 UObject（不是 Actor），直接调 GetWorld() 会返回 nullptr。
// 这里沿 Outer 链 → 找到所属 Pawn → 从 Pawn 拿 World。
//
// 为什么重要：
//   SpawnActorDeferred、SetTimer、GetTimeSeconds 等都需要有效 World。
//   没有这个覆写，装备实例里任何需要 World 的操作都会失败。
//
UWorld* UDark_TdoreEquipmentInstance::GetWorld() const
{
	if (const APawn* OwningPawn = GetPawn())
	{
		return OwningPawn->GetWorld();
	}

	// Pawn 为空的情况极少见（只可能在构造/析构的短暂窗口出现）
	return nullptr;
}

// ============================================================================
// GetLifetimeReplicatedProps — 网络复制声明
// ============================================================================
// 只复制两个属性：
//   1. Instigator    — 触发者，客户端需要知道"这装备是谁的"
//   2. SpawnedActors — 生成的 Actor 列表，客户端需要同步显示
//
// 这两个属性通过装备管理器组件的 ReplicateSubobjects 或 RegisteredSubObjectList 路径复制。
//
void UDark_TdoreEquipmentInstance::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ThisClass, Instigator);
	DOREPLIFETIME(ThisClass, SpawnedActors);
}

// ============================================================================
// GetPawn — 从 Outer 链获取所属 Pawn
// ============================================================================
// 装备管理组件创建实例时用 Owner Pawn 作为 Outer：
//   NewObject<UDark_TdoreEquipmentInstance>(OwnerPawn, InstanceType)
//
// 这意味着：
//   - 装备实例的生命周期和 Pawn 绑定（Pawn 销毁 → GC 回收实例）
//   - GetPawn() = GetOuter() 不需要额外存引用
//
APawn* UDark_TdoreEquipmentInstance::GetPawn() const
{
	return Cast<APawn>(GetOuter());
}

// ============================================================================
// GetTypedPawn — 带类型检查的 Pawn 获取
// ============================================================================
// 蓝图里经常需要拿具体的 Character 子类。
// 不用这个函数的话，蓝图要在每个使用点都做 Cast，散落到处。
// 这里统一做类型检查，不安全的情况返回 nullptr。
//
APawn* UDark_TdoreEquipmentInstance::GetTypedPawn(TSubclassOf<APawn> PawnType) const
{
	APawn* Result = nullptr;
	if (UClass* ActualPawnType = PawnType)
	{
		// GetOuter() 必须是 PawnType 或它的子类，才安全转换
		if (GetOuter() && GetOuter()->IsA(ActualPawnType))
		{
			Result = Cast<APawn>(GetOuter());
		}
	}
	return Result;
}

// ============================================================================
// SpawnEquipmentActors — 生成装备的可见 Actor
// ============================================================================
// 这是整个装备系统的"模型显示"核心。
//
// 完整流程：
//   1. 获取 OwningPawn 和 World
//   2. 确定挂接目标：
//      - Character → 挂到 Mesh（骨骼网格体），让骨骼插槽生效
//      - 非 Character Pawn → 退回到 RootComponent
//   3. 遍历 ActorsToSpawn 配置数组：
//      a. 跳过配置不完整的条目（无 ActorToSpawn 或无 AttachTarget）
//      b. SpawnActorDeferred → 延迟生成（方便后续扩展注入数据）
//      c. FinishSpawning → 完成生成
//      d. SetActorRelativeTransform + AttachToComponent → 设置相对变换并挂接
//      e. 如果是 WeaponActor → InitializeFromEquipmentInstance 绑定装备实例
//      f. 加入 SpawnedActors 列表
//
// 为什么用 Deferred Spawn？
//   FinishSpawning 之前可以对 Actor 做任意初始化（设置拥有者、注入数据等）。
//   当前还没用到，但预留这个能力比直接 SpawnActor 更灵活。
//
void UDark_TdoreEquipmentInstance::SpawnEquipmentActors(const TArray<FDark_TdoreEquipmentActorToSpawn>& ActorsToSpawn)
{
	APawn* OwningPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!OwningPawn || !World)
	{
		return;
	}

	// 对 Character 优先挂到 Mesh：hand_rSocket、weapon_rSocket 等骨骼插槽需要 Mesh 做父组件
	USceneComponent* AttachTarget = OwningPawn->GetRootComponent();
	if (ACharacter* Character = Cast<ACharacter>(OwningPawn))
	{
		AttachTarget = Character->GetMesh();
	}

	for (const FDark_TdoreEquipmentActorToSpawn& SpawnInfo : ActorsToSpawn)
	{
		// 防御性检查：缺少 Actor 类或挂接目标是无效配置
		if (!SpawnInfo.ActorToSpawn || !AttachTarget)
		{
			continue;
		}

		// Deferred Spawn：先创建未初始化的 Actor，FinishSpawning 前可注入数据
		// OwningPawn 作为 Outer，Actor 生命周期跟随 Pawn
		AActor* NewActor = World->SpawnActorDeferred<AActor>(SpawnInfo.ActorToSpawn, FTransform::Identity, OwningPawn);
		if (!NewActor)
		{
			continue;
		}

		// FinishSpawning 触发 BeginPlay、Component Initialize 等初始化流程
		NewActor->FinishSpawning(FTransform::Identity, true);

		// 设置装备 Actor 相对于挂接点的局部变换（位置、旋转、缩放）
		NewActor->SetActorRelativeTransform(SpawnInfo.AttachTransform);

		// KeepRelativeTransform：挂接时保留相对变换，不重置位置
		NewActor->AttachToComponent(AttachTarget, FAttachmentTransformRules::KeepRelativeTransform, SpawnInfo.AttachSocket);

		// 如果是武器 Actor，绑定装备实例——让武器 Actor 能通过 GetOwningEquipmentInstance 访问装备数据
		if (ADark_TdoreWeaponActor* WeaponActor = Cast<ADark_TdoreWeaponActor>(NewActor))
		{
			WeaponActor->InitializeFromEquipmentInstance(this);
		}

		// 加入生成列表：后续 DestroyEquipmentActors 用它找到需要销毁的 Actor
		SpawnedActors.Add(NewActor);
	}
}

// ============================================================================
// DestroyEquipmentActors — 销毁装备生成的所有 Actor
// ============================================================================
// 简洁直接：遍历 SpawnedActors → Destroy() → Reset() 列表
//
// 注意：
//   - Actor::Destroy() 会触发 EndPlay、清理组件、移除碰撞等
//   - SpawnedActors.Reset() 清空数组但不 delete（Actor 由 UE GC 管理）
//   - 不会影响角色自己创建的任何 Actor 或组件
//
void UDark_TdoreEquipmentInstance::DestroyEquipmentActors()
{
	for (AActor* Actor : SpawnedActors)
	{
		if (Actor)
		{
			Actor->Destroy();
		}
	}
	SpawnedActors.Reset();
}

// ============================================================================
// OnEquipped — 装备完成回调
// ============================================================================
// 被 EquipmentManager::EquipItem 在 AddEntry 之后调用。
//
// C++ 子类覆写此方法来做 C++ 层级的装备逻辑。
// 例如 UDark_TdoreWeaponInstance 覆写它来：
//   1. 设置 Status.Weapon.Equipped Tag
//   2. 挂载 ABP_ItemAnimLayers_Sword 动画层
//   3. 播放拔刀 Montage
//
// 基类在这里调用蓝图事件 K2_OnEquipped，让蓝图子类做表现层扩展。
//
// 网络触发：
//   - 服务器本地：EquipItem 里直接调用
//   - 客户端：FastArray PostReplicatedAdd 里调用
//
void UDark_TdoreEquipmentInstance::OnEquipped()
{
	K2_OnEquipped();
}

// ============================================================================
// OnUnequipped — 卸下回调
// ============================================================================
// 被 EquipmentManager::UnequipItem / FastArray PreReplicatedRemove 调用。
//
// C++ 子类覆写此方法来做清理。
// 例如 UDark_TdoreWeaponInstance 覆写它来：
//   1. 移除 Status.Weapon.Equipped Tag
//   2. 卸载动画层
//   3. 播放收刀 Montage
//
// 基类在这里调用蓝图事件 K2_OnUnequipped。
//
void UDark_TdoreEquipmentInstance::OnUnequipped()
{
	K2_OnUnequipped();
}

// ============================================================================
// OnRep_Instigator — Instigator 复制回调
// ============================================================================
// 当前为空实现，预留以下扩展场景：
//   - 客户端收到 Instigator 后刷新 UI（显示武器来源）
//   - 根据 Instigator 切换到不同的动画层（不同角色用不同握持姿势）
//   - 调试/统计：记录装备转移历史
//
void UDark_TdoreEquipmentInstance::OnRep_Instigator()
{
}
