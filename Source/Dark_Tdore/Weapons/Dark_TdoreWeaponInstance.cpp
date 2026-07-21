// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapons/Dark_TdoreWeaponInstance.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Animation/Dark_TdoreAnimInstance.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dark_Tdore.h"
#include "GameFramework/Character.h"
#include "NativeGameplayTags.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreWeaponInstance)

// ============================================================================
// 注册 GameplayTag: "Status.Weapon.Equipped"
// ============================================================================
// UE_DEFINE_GAMEPLAY_TAG_STATIC 做了三件事：
//   1. 声明一个 FNativeGameplayTag 变量 TAG_Status_Weapon_Equipped
//   2. 在引擎初始化时把字符串 "Status.Weapon.Equipped" 注册进 Tag 管理器
//   3. STATIC 表示只在本 .cpp 内可见（其他文件用 UE_DECLARE_GAMEPLAY_TAG_EXTERN 引用）
//
// AnimInstance 在 RefreshWeaponState() 里读这个 Tag：
//   if (ASC->HasMatchingGameplayTag(TAG_Status_Weapon_Equipped))
//       bHasWeapon = true;
//
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Status_Weapon_Equipped, "Status.Weapon.Equipped");

// ============================================================================
// 构造函数
// ============================================================================
// ObjectInitializer：UE 的对象初始化器，传递给父类构造
// Super(ObjectInitializer)：先调用父类 UDark_TdoreEquipmentInstance 的构造
//
// 这里不需要初始化任何成员，因为：
//   TimeLastEquipped = 0.0     （头文件里声明时的默认值）
//   TimeLastFired = 0.0        （同上）
//   LinkedAnimLayer = nullptr  （TSubclassOf 默认为空）
//   EquippedAnimSet / UnequippedAnimSet 是 UPROPERTY，由蓝图配置
//
UDark_TdoreWeaponInstance::UDark_TdoreWeaponInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)  // 初始化列表：先构造父类
{
	// 函数体为空：所有成员已在头文件中用 = 设置了默认值
}

// ============================================================================
// OnEquipped — 装备回调（虚函数覆写）
// ============================================================================
// 这是整个武器装备流程的 C++ 入口。
// 谁调用它？
//   - 服务器：EquipmentManager::EquipItem() → Result->OnEquipped()
//   - 客户端：FastArray PostReplicatedAdd → Entry.Instance->OnEquipped()
//
// 覆写关系：
//   UDark_TdoreEquipmentInstance::OnEquipped()    ← 基类（空实现，只调蓝图事件）
//       ↑ override
//   UDark_TdoreWeaponInstance::OnEquipped()       ← 这里（加武器专属逻辑）
//
void UDark_TdoreWeaponInstance::OnEquipped()
{
	// ===== 步骤1：记录装备时间 =====
	// GetWorld()：通过 Outer→Pawn→World 链取得当前关卡世界指针
	// GetTimeSeconds()：返回游戏自启动以来经过的秒数（不受暂停影响）
	// 三元运算符：World 为空时兜底写 0.0（理论上不会为空，但防御性编程）
	// TimeLastEquipped 用途：GetTimeSinceLastInteractedWith() 用它算闲置时长
	TimeLastEquipped = GetWorld()
		? GetWorld()->GetTimeSeconds()  // 如果 World 存在，取当前游戏时间
		: 0.0;                          // 如果 World 为空，兜底 0

	// ===== 步骤2：给 ASC 设置装备 Tag =====
	// true 参数 → SetLooseGameplayTagCount("Status.Weapon.Equipped", 1)
	// 这会让 AnimInstance 检测到 bHasWeapon=true，触发 BlendPosesByBool 切到武器动画分支
	SetWeaponEquippedTag(true);

	// ===== 步骤3：挂载武器动画层 =====
	// LinkAnimLayer(true) 做了三件事：
	//   1. 如果旧动画层还在 → UnlinkAnimClassLayers 卸载
	//   2. 从 EquippedAnimSet 选动画层类（如 ABP_ItemAnimLayers_Sword）
	//   3. MeshComponent->LinkAnimClassLayers(AnimLayer) 注入
	// 返回值：true=成功挂载，false=失败（Mesh 为空或 EquippedAnimSet 未配置）
	const bool bLinkedAnimLayer = LinkAnimLayer(true);

	// ===== 步骤4：动画层失败则回退 Tag =====
	// 为什么必须回退？
	//   Tag 和动画层必须同步：
	//     - Tag=true + 动画层 ok  → 正常武器状态 ✓
	//     - Tag=true + 动画层空   → 武器表现错误（拿空气） ✗
	//     - Tag=false + 动画层 ok → 动画层白挂上 ✗
	//   回退保证状态一致性
	if (!bLinkedAnimLayer)
	{
		// false → SetLooseGameplayTagCount("Status.Weapon.Equipped", 0) = 移除 Tag
		SetWeaponEquippedTag(false);
	}

	// ===== 步骤5：通知父类 =====
	// Super::OnEquipped() → UDark_TdoreEquipmentInstance::OnEquipped()
	// → 调用蓝图事件 K2_OnEquipped()
	// 蓝图子类（BP_Sword 等）在这里加装备特效、音效、UI 等表现层逻辑
	Super::OnEquipped();

	// ===== 步骤6：播放拔刀 Montage（如果配置了） =====
	// EquipMontage 在蓝图子类的 Class Defaults 中设置
	// 如果为空 → PlayWeaponMontage 内部判断后跳过
	PlayWeaponMontage(EquipMontage);
}

// ============================================================================
// OnUnequipped — 卸下回调（虚函数覆写）
// ============================================================================
// 谁调用它？
//   - 服务器：EquipmentManager::UnequipItem() → ItemInstance->OnUnequipped()
//            EquipmentManager::UninitializeComponent() → UnequipItem → OnUnequipped
//   - 客户端：FastArray PreReplicatedRemove → Entry.Instance->OnUnequipped()
//
void UDark_TdoreWeaponInstance::OnUnequipped()
{
	// ===== 步骤1：移除装备 Tag =====
	// false → SetLooseGameplayTagCount("Status.Weapon.Equipped", 0)
	// Tag 移除后，AnimInstance 下次 RefreshWeaponState() 读到 bHasWeapon=false
	SetWeaponEquippedTag(false);

	// ===== 步骤2：卸载动画层 =====
	// LinkAnimLayer(false) 做了：
	//   1. 如果 LinkedAnimLayer 不为空 → MeshComponent->UnlinkAnimClassLayers
	//   2. LinkedAnimLayer = nullptr（清空缓存）
	LinkAnimLayer(false);

	// ===== 步骤3：播放收刀 Montage =====
	PlayWeaponMontage(UnequipMontage);

	// ===== 步骤4：通知父类 =====
	// → 调用蓝图事件 K2_OnUnequipped()
	Super::OnUnequipped();
}

// ============================================================================
// UpdateFiringTime — 更新最近攻击/开火时间
// ============================================================================
// 每次武器攻击时调用（近战挥砍命中 / 远程开枪）
// 和 TimeLastEquipped 一起被 GetTimeSinceLastInteractedWith 用来算"多久没交互了"
// 使用 GetWorld()->GetTimeSeconds() 保证时间线一致（都用同一个时间基准）
//
void UDark_TdoreWeaponInstance::UpdateFiringTime()
{
	// 直接写时间，无需复杂逻辑
	// GetTimeSeconds() 是 double 精度，存到同样 double 的 TimeLastFired
	TimeLastFired = GetWorld()
		? GetWorld()->GetTimeSeconds()  // World 存在 → 取游戏时间
		: 0.0;                          // World 空 → 兜底（基本不会走到）
}

// ============================================================================
// GetTimeSinceLastInteractedWith — 距上次交互经过了多少秒
// ============================================================================
// 返回：当前时间 - 最近一次交互时间（装备 或 攻击，取较晚的）
// 返回 float：足够 UI 使用，不需要 double 精度
//
// 调用场景：
//   - 蓝图里 Tick 调用，控制准星扩散系数
//   - UI Widget 的百分比条（冷却进度）
//   - 超过 N 秒自动收刀动画的判断条件
//
float UDark_TdoreWeaponInstance::GetTimeSinceLastInteractedWith() const
{
	// 第一步：获取 World
	// const 成员函数 → 不能修改任何成员变量
	const UWorld* World = GetWorld();

	// 第二步：World 空 → 安全兜底
	// 发生场景：装备实例正在被 GC 回收时访问
	if (!World)
	{
		return 0.0f;
	}

	// 第三步：取装备时间和攻击时间中较晚的那个作为"最后交互时间"
	// FMath::Max(a, b)：返回 a 和 b 中较大的值
	// 场景举例：
	//   TimeLastEquipped = 10.0（10秒前装备了武器）
	//   TimeLastFired     = 13.0（3秒前攻击了一次）
	//   → Max(10.0, 13.0) = 13.0  → 最后交互 = 攻击时间
	//
	//   TimeLastEquipped = 10.0（10秒前装备了武器）
	//   TimeLastFired     = 0.0 （从未攻击）
	//   → Max(10.0, 0.0)  = 10.0  → 最后交互 = 装备时间
	const double LastInteractionTime = FMath::Max(TimeLastEquipped, TimeLastFired);

	// 第四步：当前时间 - 最后交互时间 = 闲置时长
	// static_cast<float>：double → float（这个精度损失不影响 UI 显示）
	return static_cast<float>(World->GetTimeSeconds() - LastInteractionTime);
}

// ============================================================================
// PickAnimLayer — 选择动画层（简化版）
// ============================================================================
// 不传 CosmeticTags，等价于 PickBestAnimLayer(bEquipped, 空容器)。
// 当前够用：所有同类型武器用同一个动画层类。
// 后续需要区分不同角色体型时用 PickBestAnimLayer。
//
// 参数 bEquipped：
//   true  → 从 EquippedAnimSet 中选（装备后使用的层）
//   false → 从 UnequippedAnimSet 中选（卸下/空手时用的层）
//
// 返回值：TSubclassOf<UAnimInstance> → 动画蓝图类，如 ABP_ItemAnimLayers_Sword
//
TSubclassOf<UAnimInstance> UDark_TdoreWeaponInstance::PickAnimLayer(bool bEquipped) const
{
	// 委托给完整版，传空 Tag 容器（不区分体型/性别等差异）
	return PickBestAnimLayer(bEquipped, FGameplayTagContainer());
}

// ============================================================================
// PickBestAnimLayer — 选择最佳动画层（完整版，支持 CosmeticTags 过滤）
// ============================================================================
// 参数：
//   bEquipped   ：装备 or 空手 → 决定用哪个选择集
//   CosmeticTags：外貌标签（如 BodyType.Male、Skin.Variant1）
//                后续可用来为不同角色体型选择不同的武器持握动画
//
// 内部调用：SetToQuery.SelectBestLayer(CosmeticTags)
//   根据 Tags 的权重匹配最合适的层；没有匹配项 → 返回默认层（数组第一项）
//
TSubclassOf<UAnimInstance> UDark_TdoreWeaponInstance::PickBestAnimLayer(
	bool bEquipped,                                  // true=装备, false=空手
	const FGameplayTagContainer& CosmeticTags) const  // 外貌标签（当前传空容器）
{
	// 三元运算符：根据 bEquipped 选择动画层集合
	// EquippedAnimSet   → UPROPERTY，蓝图子类 Class Defaults 中配置（如 ABP_ItemAnimLayers_Sword）
	// UnequippedAnimSet → UPROPERTY，当前留空（空手动画由 ABP_Character_Base 自己处理）
	const FDark_TdoreAnimLayerSelectionSet& SetToQuery =
		bEquipped ? EquippedAnimSet : UnequippedAnimSet;

	// SelectBestLayer：遍历 SetToQuery 里的所有层配置
	// 每个层配置包含：AnimLayer（动画蓝图类）+ Tags（匹配标签）
	// 匹配规则：CosmeticTags 和层的 Tags 交集越大的权重越高
	// 无匹配 → 返回数组第一个元素的 AnimLayer（默认层）
	return SetToQuery.SelectBestLayer(CosmeticTags);
}

// ============================================================================
// LinkAnimLayer — 挂载 / 卸载武器动画层
// ============================================================================
// 这是整个动画分层系统的核心执行函数。
//
// bEquipped = true（装备路径）：
//   ┌──────────────────────────────────────────┐
//   │ 1. Cast<ACharacter>(GetPawn())           │ 拿到角色
//   │ 2. Character->GetMesh()                  │ 拿到骨骼网格体
//   │ 3. if (LinkedAnimLayer) → Unlink        │ 先卸旧的
//   │ 4. PickAnimLayer(true) → 选动画层类      │
//   │ 5. Mesh->LinkAnimClassLayers(AnimLayer)  │ 注入！
//   │ 6. LinkedAnimLayer = AnimLayer           │ 缓存（供卸下用）
//   └──────────────────────────────────────────┘
//
// bEquipped = false（卸下路径）：
//   ┌──────────────────────────────────────────┐
//   │ 1. 拿 Character + Mesh                   │
//   │ 2. if (LinkedAnimLayer) → Unlink        │ 卸载
//   │ 3. LinkedAnimLayer = nullptr             │ 清空缓存
//   └──────────────────────────────────────────┘
//
// 返回值：
//   装备→ true=成功挂载了动画层 / false=失败（Mesh null 或未配置动画层）
//   卸下→ 始终 true
//
bool UDark_TdoreWeaponInstance::LinkAnimLayer(bool bEquipped)
{
	// ===== 获取角色和 Mesh =====
	// GetPawn()：从 Outer（OwningPawn）拿 Pawn 指针
	// Cast<ACharacter>：只有 ACharacter 才有 SkeletalMeshComponent（GetMesh）
	//   如果是 APawn（非 Character）→ Cast 失败 → Character = nullptr
	ACharacter* Character = Cast<ACharacter>(GetPawn());

	// 如果 Character 不为空 → 取它的 Mesh（SkeletalMeshComponent）
	// Character 为空 → MeshComponent = nullptr → 下面会 return false
	USkeletalMeshComponent* MeshComponent = Character ? Character->GetMesh() : nullptr;

	// ===== 防御检查：Mesh 必须存在 =====
	if (!MeshComponent)
	{
		return false;  // 没有 Mesh 无法挂载动画层，告诉调用方失败了
	}

	// ===== 如果之前已经挂过动画层，先卸载旧的 =====
	// 什么时候会走到？
	//   1. 同一角色换武器：剑→换斧头，先卸载剑的动画层再挂斧头的
	//   2. OnEquipped 被重复调用（网络延迟导致）
	// LinkedAnimLayer 是 TSubclassOf<UAnimInstance>，记录了上次挂的层类
	if (LinkedAnimLayer)
	{
		// UnlinkAnimClassLayers(旧层类)：
		//   UE 引擎从 Mesh 的动画系统中移除这个层的注册
		//   之后 AnimBP 的 Linked Anim Layer 节点不再路由到它
		MeshComponent->UnlinkAnimClassLayers(LinkedAnimLayer);

		// 清空缓存：已卸载完成
		LinkedAnimLayer = nullptr;
	}

	// ===== 装备路径：挂载新动画层 =====
	if (bEquipped)
	{
		// PickAnimLayer(true)：
		//   从 EquippedAnimSet（蓝图 Class Defaults 中配置）选动画层类
		//   例如：剑的 EquippedAnimSet 配置了 ABP_ItemAnimLayers_Sword
		const TSubclassOf<UAnimInstance> AnimLayer = PickAnimLayer(true);

		// 动画层类为空 → 说明蓝图子类忘记在 EquippedAnimSet 里配置了
		if (!AnimLayer)
		{
			return false;  // 告诉 OnEquipped 动画层挂载失败，它会回退 Tag
		}

		// LinkAnimClassLayers：UE5 的 Linked Anim Layer 核心 API
		// 参数：TSubclassOf<UAnimInstance> → 动画蓝图类
		// 效果：把这个动画蓝图"注入"到角色 Mesh 的动画系统中
		//       ABP_Character_Base 里的 LinkedAnimLayer 节点会自动路由到这个层
		//       层的 AnimGraph 输出会被混合到最终姿态中
		MeshComponent->LinkAnimClassLayers(AnimLayer);

		// 缓存动画层类 — 供卸下时 UnlinkAnimClassLayers 精确卸载
		LinkedAnimLayer = AnimLayer;

		return true;  // 挂载成功
	}

	// ===== 卸下路径：已经在上面 Unlink 完了 =====
	// bEquipped=false 会走到这里
	return true;  // 卸下成功（Unlink 在上面的 if(LinkedAnimLayer) 块里做完了）
}

// ============================================================================
// SetWeaponEquippedTag — 把装备状态同步到 ASC
// ============================================================================
// 这是连接"装备系统"和"GAS/动画系统"的桥梁函数。
//
// const 成员函数：不修改武器实例自己的成员变量
//
// 参数 bEquipped：
//   true  → 正在装备 → 添加 Status.Weapon.Equipped Tag
//   false → 正在卸下 → 移除 Status.Weapon.Equipped Tag
//
// 影响范围：
//   1. AnimInstance::RefreshWeaponState() → bHasWeapon = true/false → ABP 动画分支切换
//   2. GameplayAbility 的 ActivationBlockedTags / CancelAbilitiesWithTag
//      可以通过这个 Tag 限制技能使用（如"没装备武器时不能放技能"）
//
void UDark_TdoreWeaponInstance::SetWeaponEquippedTag(bool bEquipped) const
{
	// ===== 第 1 步：获取 Pawn =====
	// GetPawn()：从 Outer 链获取拥有这个武器实例的 Pawn
	APawn* OwningPawn = GetPawn();

	// ===== 第 2 步：从 Pawn 获取 ASC =====
	// UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor)：
	//   自动查找 Actor 上的 ASC。
	//   对于玩家：ASC 在 PlayerState 上 → 这个函数会自动沿 IAbilitySystemInterface 找到
	//   对于 NPC：ASC 可能直接在 Pawn 上
	// 如果 Pawn 为空 → 不执行三元运算符的第二个分支 → ASC = nullptr
	UAbilitySystemComponent* ASC = OwningPawn
		? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwningPawn)
		: nullptr;

	// ===== 分支 A：ASC 存在 → 正常设置 Tag =====
	if (ASC)
	{
		// SetLooseGameplayTagCount(Tag, Count)：
		//   Count = 1 → 添加 Tag（如果已存在，计数 +1）
		//   Count = 0 → 移除 Tag（计数归零，Tag 消失）
		//
		// 为什么用 Count 而不是 Set？
		//   防止嵌套调用：比如 OnEquipped 被调了两次但还没 OnUnequipped
		//   → Count 会变成 2，需要两次 SetWeaponEquippedTag(false) 才能真正移除
		//   如果用 Set（布尔），第一次 Set(false) 就错误地移除了 Tag
		//
		// bEquipped ? 1 : 0：
		//   true  → 1  → 添加 Tag
		//   false → 0  → 移除 Tag
		//
		// TAG_Status_Weapon_Equipped：
		//   就是文件顶部注册的 "Status.Weapon.Equipped" 的句柄
		ASC->SetLooseGameplayTagCount(
			TAG_Status_Weapon_Equipped,   // 目标 Tag
			bEquipped ? 1 : 0);            // 计数：1=加, 0=删
	}
	// ===== 分支 B：ASC 不存在 → Tag 设置失败 =====
	// else 分支不做任何事 — 没有 ASC 就没有 Tag 可设
	// 场景：Pawn 还没 PossessedBy、Pawn 没实现 IAbilitySystemInterface

	// ===== 第 3 步：立即通知 AnimInstance 刷新武器状态 =====
	// 为什么不等到下一帧的 NativeUpdateAnimation？
	//   LinkAnimClassLayers 和 SetLooseGameplayTagCount 可能在非同帧执行。
	//   如果先 LinkAnimClassLayers 再等下一帧 NativeUpdateAnimation 读 Tag，
	//   会有一帧的时间差：动画层挂上了但 BlendPosesByBool 还没切过来。
	//   这一帧武器动画层会输出但是不被 BlendPosesByBool 选中 → 表现卡顿/闪烁。
	//
	//   立即调 RefreshWeaponState 保证：Link + Tag 在同一帧生效。

	// Cast<ACharacter>：只有 Character 有 GetMesh() 和 AnimInstance
	if (ACharacter* Character = Cast<ACharacter>(OwningPawn))
	{
		// 拿骨骼网格体组件
		if (USkeletalMeshComponent* MeshComponent = Character->GetMesh())
		{
			// GetAnimInstance()：返回当前运行的动画蓝图实例（ABP_Character_Base 实例）
			// Cast<UDark_TdoreAnimInstance>：必须是我们的 AnimInstance 子类才调用
			if (UDark_TdoreAnimInstance* AnimInstance =
				Cast<UDark_TdoreAnimInstance>(MeshComponent->GetAnimInstance()))
			{
				// RefreshWeaponState() 做两件事：
				//   1. 重新从 ASC 检查 Status.Weapon.Equipped Tag
				//   2. 从 EquipmentManagerComponent 检查是否有 WeaponInstance
				//   3. 把结果写入 bHasWeapon（bool，蓝图可读）
				AnimInstance->RefreshWeaponState();
			}
		}
	}
}

// ============================================================================
// PlayWeaponMontage — 播放武器 Montage（拔刀/收刀）
// ============================================================================
// 简单工具函数，包装了 Character::PlayAnimMontage。
// 包装的好处：
//   - 统一做空指针检查（Character + MontageToPlay 都不为空）
//   - 调用方不需要每次写 if (Character && Montage)
//   - 后续可扩展（如加播放失败日志、条件检查等）
//
// 参数 MontageToPlay：要播放的蒙太奇资产指针
//   - EquipMontage（拔刀）或 UnequipMontage（收刀）
//   - 如果蓝图里没配（nullptr）→ 跳过不播放
//
void UDark_TdoreWeaponInstance::PlayWeaponMontage(UAnimMontage* MontageToPlay) const
{
	// GetPawn() → Cast<ACharacter>：Montage 只能在 Character 上播放
	ACharacter* Character = Cast<ACharacter>(GetPawn());

	// 双重条件：
	//   1. Character 不为空（否则没法播 Montage）
	//   2. MontageToPlay 不为空（蓝图可能没配置拔刀/收刀动画）
	// 只有两个条件都满足才播放
	if (Character && MontageToPlay)
	{
		// PlayAnimMontage：
		//   把 Montage 资产推入角色动画队列
		//   如果配了 Slot（如 DefaultSlot），会占住这个 Slot 直到播完
		//   调用 AnimBP 的 Montage 节点来混合输出
		Character->PlayAnimMontage(MontageToPlay);
	}
	// 如果 Character 不空但 Montage 为空 → 跳过（合理：还没配动画资源）
	// 如果 Character 为空 → 跳过（说明在非 Character Pawn 上调了，不该发生但安全兜底）
}
