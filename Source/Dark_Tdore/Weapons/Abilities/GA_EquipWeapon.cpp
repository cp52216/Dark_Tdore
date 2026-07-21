// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapons/Abilities/GA_EquipWeapon.h"

#include "Equipment/Dark_TdoreEquipmentDefinition.h"
#include "Equipment/Dark_TdoreEquipmentInstance.h"
#include "Equipment/Dark_TdoreEquipmentManagerComponent.h"
#include "Weapons/Dark_TdoreWeaponInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GA_EquipWeapon)

// ============================================================================
// 构造函数
// ============================================================================
UGA_EquipWeapon::UGA_EquipWeapon(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 按一次键激活一次即可。比如按 2 → 装备剑，不需要按住 2。
	ActivationPolicy = EDark_TdoreAbilityActivationPolicy::OnInputTriggered;

	// 同组互斥：如果这个技能已经激活，新按键会取消旧的重新激活。
	// 防止连续快速按 2 → 3 → 2 导致装备列表被并发修改。
	ActivationGroup = EDark_TdoreAbilityActivationGroup::Exclusive_Replaceable;

	// 只在服务器执行：装备列表由服务器权威修改，客户端通过 FastArray 同步。
	// 客户端按键 → 服务器 RPC 激活这个技能 → 服务器修改装备列表 → 复制给客户端。
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;

	// 每个角色一个独立实例，不共享 CDO。
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

// ============================================================================
// CanActivateAbility — 激活条件检查
// ============================================================================
bool UGA_EquipWeapon::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	// 父类检查：Cost、Cooldown、ActivationBlockedTags 等通用条件
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	// 自己的类 UPROPERTY 必须在蓝图子类中填好
	if (!EquipmentDefinition || !ActorInfo || !ActorInfo->AvatarActor.IsValid())
	{
		return false;
	}

	// AvatarActor（Character）上必须有 EquipmentManagerComponent。
	// 没有装备管理器 → 无法装备任何东西 → 技能不可用。
	return ActorInfo->AvatarActor->FindComponentByClass<UDark_TdoreEquipmentManagerComponent>() != nullptr;
}

// ============================================================================
// ActivateAbility — 执行装备/卸下逻辑
// ============================================================================
// 这是 GA_EquipWeapon 的核心：它不做任何装备细节，完全委托给装备系统。
//
// 为什么这个 Ability 要牵扯装备系统？
//
//   角色按下 2 键 → ASC 路由到 GA_EquipWeapon → 这里要做的事是"装备剑"。
//   "装备剑"非常复杂，涉及：
//     1. 创建 UDark_TdoreWeaponInstance 运行时实例
//     2. 授予剑的 AbilitySet（连击技能、属性 GE 等）
//     3. 生成剑的 3D 模型 Actor（SpawnActor + 挂到骨骼插槽）
//     4. 挂载剑的动画层（LinkAnimClassLayers → ABP_ItemAnimLayers_Sword）
//     5. 设置 Status.Weapon.Equipped GameplayTag（让 AnimBP 感知）
//     6. 播放拔刀动画
//     7. 将上面的状态网络复制到客户端
//
//   如果一个 Ability 直接做这些事，代码会非常臃肿，
//   而且其他系统（如 GM 命令、保存/加载、编辑器测试）也需要"装备武器"的能力。
//
//   所以架构上拆成两层：
//
//   [ 表现层 / 输入触发 ]           [ 装备管理核心 ]
//   ┌────────────────────┐         ┌──────────────────────┐
//   │  GA_EquipWeapon    │ ──────→ │ EquipmentManager     │
//   │  (GAS 技能)         │  调用   │ (角色上的组件)        │
//   │                    │         │                      │
//   │  - 检查条件        │         │  - EquipItem()        │
//   │  - 判断Toggle卸下   │         │     → AddEntry       │
//   │  - 调用装备管理器   │         │       创建实例        │
//   └────────────────────┘         │       授予技能        │
//                                   │       生成Actor       │
//   GA_EquipWeapon 只负责：         │       OnEquipped      │
//     - "什么时候装备"（按键触发）   │       网络复制        │
//     - "要不要Toggle卸下"          │                      │
//     - "要不要先卸别的武器"        │  - UnequipItem()      │
//                                   │     → 回收技能/GE     │
//                                   │       销毁Actor       │
//                                   │       OnUnequipped    │
//                                   └──────────────────────┘
//
void UGA_EquipWeapon::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 父类激活（设置 Ability 状态为 Active）
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// CommitAbility：扣除 Cost + 检查 Cooldown。
	// 失败（如 Cost 不够）→ 立刻结束技能。
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);  // true,true = 取消且不通知
		return;
	}

	// ===== 获取角色和装备管理器 =====
	// AvatarActor：技能的作用目标，即 Character/Pawn
	// ActorInfo->AvatarActor.Get()：TWeakObjectPtr 取值
	AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;

	// FindComponentByClass：在当前 Actor 的所有组件中找 EquipmentManagerComponent
	// 这个组件在 ADark_TdoreCharacter 构造中创建（CreateDefaultSubobject）
	UDark_TdoreEquipmentManagerComponent* EquipmentManager =
		AvatarActor
			? AvatarActor->FindComponentByClass<UDark_TdoreEquipmentManagerComponent>()
			: nullptr;

	// 防御：Actor 或 Manager 为空 → 不能装备
	if (!EquipmentManager || !EquipmentDefinition)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// ===== 分支 A：目标武器已经装备了 =====
	// GetFirstInstanceOfDefinition(EquipmentDefinition)：
	//   遍历装备列表，按"装备定义类"匹配（不是按实例类型）。
	//   例如：B_EquipmentDefinition_Sword 和 B_EquipmentDefinition_Axe
	//   共用同一个 WeaponInstance 子类，但定义不同。
	//   用 Definition 匹配保证精确找到"这把剑"是否已装备。
	//
	if (UDark_TdoreEquipmentInstance* ExistingInstance =
		EquipmentManager->GetFirstInstanceOfDefinition(EquipmentDefinition))
	{
		// bToggleOffIfAlreadyEquipped：
		//   true  → 再按一次相同按键卸下（Toggle 行为：2 拿剑 → 2 收剑）
		//   false → 什么也不做，保持装备状态（防止误触卸下）
		//
		if (bToggleOffIfAlreadyEquipped)
		{
			// UnequipItem 流程：
			//   1. OnUnequipped（移除 Tag、卸载动画层、播放收刀 Montage）
			//   2. RemoveEntry（回收技能/GE、销毁 Actor、移除 FastArray 条目）
			EquipmentManager->UnequipItem(ExistingInstance);
		}

		// 无论是否执行了卸下，技能都结束（装备状态已经处理完了）
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);  // false = 正常结束
		return;
	}

	// ===== 分支 B：目标武器还没装备 → 执行装备 =====

	// bUnequipOtherWeapons：
	//   true  → 装备新武器前先卸下所有 WeaponInstance（主武器槽只保留一把）
	//   false → 允许同时持有多个武器（如主武器 + 副手枪）
	//
	if (bUnequipOtherWeapons)
	{
		// UnequipAllItemsOfType(UDark_TdoreWeaponInstance::StaticClass())：
		//   遍历装备列表，找到所有 IsA(WeaponInstance) 的实例 → 逐个 UnequipItem。
		//   为什么按类型而不是按定义？
		//     当前只处理"武器槽"逻辑：不管现在装备的是剑还是斧头，
		//     只要是 WeaponInstance，装备新武器时都卸掉。
		//     其他类型装备（如盔甲、饰品）不受影响。
		//
		EquipmentManager->UnequipAllItemsOfType(UDark_TdoreWeaponInstance::StaticClass());
	}

	// ===== 执行装备 =====
	// EquipItem(EquipmentDefinition) 做三件事：
	//   1. AddEntry：创建实例 + 授予 AbilitySet + 生成 Actor + 标记 FastArray 脏
	//   2. OnEquipped：设置装备 Tag + 挂载动画层 + 播放拔刀 Montage
	//   3. AddReplicatedSubObject：让装备 UObject 参与网络复制
	//
	// EquipmentDefinition 是蓝图子类 Class Defaults 中填的
	// 例如 GA_EquipWeapon_Sword 的 EquipmentDefinition = B_EquipmentDefinition_Sword
	// EquipmentDefinition 里配置了：用哪个 Instance 类、生成哪些 Actor、授予哪些技能
	//
	EquipmentManager->EquipItem(EquipmentDefinition);

	// 装备完成后技能结束（装备是一次性动作，不需要保持激活）
	// false = 正常结束（不是取消），这意味着 GAS 可以正常走 EndAbility 生命週期
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
