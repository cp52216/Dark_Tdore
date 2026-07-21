// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreAnimInstance.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Animation/AnimEnums.h"
#include "Character/Dark_TdoreCharacterMovementComponent.h"
#include "Dark_Tdore.h"
#include "Dark_TdoreCharacter.h"
#include "Equipment/Dark_TdoreEquipmentManagerComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NativeGameplayTags.h"
#include "Weapons/Dark_TdoreWeaponInstance.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreAnimInstance)

// ============================================================================
// 注册 GameplayTag: "Status.Weapon.Equipped"
// ============================================================================
// UE_DEFINE_GAMEPLAY_TAG_STATIC：在 .cpp 内声明 + 注册一个 GameplayTag。
// TAG_Anim_Status_Weapon_Equipped 就是这个 Tag 的 C++ 变量名。
// 和 Dark_TdoreWeaponInstance.cpp 中注册的是同一个 Tag 字符串，
// 但变量名不同（这边是 TAG_Anim_Status_Weapon_Equipped，那边是 TAG_Status_Weapon_Equipped），
// 两个文件各自用各自的变量名，引用的是同一个 Tag。
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Anim_Status_Weapon_Equipped, "Status.Weapon.Equipped");

// ============================================================================
// 构造函数
// ============================================================================
UDark_TdoreAnimInstance::UDark_TdoreAnimInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)  // 先调用父类 UAnimInstance 的构造
{
	// 设置根运动模式：只从 Montage 取根运动，走跑移动不走根运动
	// ERootMotionMode 枚举值：
	//   NoRootMotionExtraction     — 完全不用根运动
	//   RootMotionFromMontagesOnly — 只有 Montage 的根运动生效（当前设置）
	//   RootMotionFromEverything   — 所有动画都提取根运动
	// 为什么用 MontagesOnly？
	//   走跑移动由 CharacterMovementComponent 驱动位移
	//   技能动画（攻击/闪避/受击）由 Montage 根运动驱动位移
	//   这样两个系统互不干扰
	RootMotionMode = ERootMotionMode::RootMotionFromMontagesOnly;
}

// ============================================================================
// InitializeWithAbilitySystem — 绑定 ASC 到 AnimInstance
// ============================================================================
// AnimInstance 初始化时（NativeInitializeAnimation）调用一次。
// 做了两件事：
//   1. 缓存 ASC 指针到 NativeAbilitySystemComponent
//      → 之后 RefreshWeaponState / NativeUpdateAnimation 直接读它，不用每帧查找
//   2. 初始化 GameplayTagPropertyMap
//      → 把 ASC 上的 GameplayTag 自动映射为 AnimBP 蓝图变量
//      → 例如：ASC 有 Gameplay.Crouching → 蓝图变量 bIsCrouching = true
//
void UDark_TdoreAnimInstance::InitializeWithAbilitySystem(UAbilitySystemComponent* ASC)
{
	// check(ASC)：如果 ASC 为空，立刻 crash 暴露问题
	// 设计意图：AnimInstance 必须绑定 ASC，否则所有 Tag 查询都失效
	check(ASC);

	// 缓存 ASC 引用 — 之后每帧 NativeUpdateAnimation 和 RefreshWeaponState 直接用
	NativeAbilitySystemComponent = ASC;

	// GameplayTagPropertyMap.Initialize(this, ASC)：
	//   遍历 AnimBP Class Defaults 中配的所有 Tag→变量映射
	//   向 ASC 注册委托：当这些 Tag 变化时，自动更新 AnimBP 变量
	GameplayTagPropertyMap.Initialize(this, ASC);
}

// ============================================================================
// RefreshWeaponState — 刷新 bHasWeapon 状态
// ============================================================================
// 两个独立数据源，任一满足即为 true：
//   来源1：ASC 上是否有 Status.Weapon.Equipped GameplayTag
//          → 由 WeaponInstance::OnEquipped 调用 SetLooseGameplayTagCount 设置
//   来源2：角色上的 EquipmentManagerComponent 里是否存在 UDark_TdoreWeaponInstance
//          → 由 EquipmentManager::EquipItem 调用 AddEntry 创建
//
// 双源互补设计：
//   - Tag 可能还没复制到客户端 → Equipment 硬检测兜底
//   - 装备实例可能在 Tag 设置前就创建了 → Tag 后续补上
//   - 两者 OR 保证过渡期不闪烁
//
// 谁调用它？
//   - NativeInitializeAnimation：初始化时调一次
//   - NativeUpdateAnimation：每帧调一次
//   - WeaponInstance::SetWeaponEquippedTag：Tag 变化后主动推通知（不用等下一帧）
//
void UDark_TdoreAnimInstance::RefreshWeaponState()
{
	// ===== 第 1 步：确保 ASC 缓存有效 =====
	// NativeAbilitySystemComponent 在 InitializeWithAbilitySystem 中设置
	// 如果还没设置（比如初始化顺序问题），这里兜底查找一次并缓存
	if (!NativeAbilitySystemComponent)
	{
		// GetOwningActor()：AnimInstance 所属的 Actor（Character）
		if (AActor* OwningActor = GetOwningActor())
		{
			// UAbilitySystemGlobals::GetAbilitySystemComponentFromActor：
			//   统一的 ASC 查找入口。对玩家从 PlayerState 找，对 NPC 从 Pawn 找
			NativeAbilitySystemComponent =
				UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwningActor);
		}
	}

	// ===== 第 2 步：保存旧值，后续判断是否变化 =====
	// bHasWeapon 被 AnimBP 的 BlendPosesByBool 节点读取
	const bool bPreviousHasWeapon = bHasWeapon;

	// ===== 第 3 步：来源1 — ASC Tag 检测 =====
	// NativeAbilitySystemComponent.Get()：TWeakObjectPtr 取值，不增加引用计数
	// HasMatchingGameplayTag(Tag)：检查 ASC 当前是否拥有指定 Tag
	//   true  → 有装备武器 Tag（WeaponInstance::OnEquipped 里 SetLooseGameplayTagCount 设置的）
	//   false → 没有或 ASC 为 nullptr
	const bool bHasWeaponFromTag =
		NativeAbilitySystemComponent  // 指针有效检查
		&& NativeAbilitySystemComponent.Get()->HasMatchingGameplayTag(
			TAG_Anim_Status_Weapon_Equipped);  // "Status.Weapon.Equipped"

	// ===== 第 4 步：来源2 — 装备管理器硬检测 =====
	bool bHasWeaponFromEquipment = false;
	if (AActor* OwningActor = GetOwningActor())
	{
		// FindComponentByClass：在当前 Actor 上找装备管理器组件
		// 这是一个 O(n) 查找，但组件数量很少，性能不是问题
		if (UDark_TdoreEquipmentManagerComponent* EquipmentManager =
			OwningActor->FindComponentByClass<UDark_TdoreEquipmentManagerComponent>())
		{
			// GetFirstInstanceOfType(UDark_TdoreWeaponInstance::StaticClass())：
			//   遍历 EquipmentList.Entries，返回第一个 IsA(WeaponInstance) 的实例
			//   返回 nullptr → 没有武器
			//   返回非 nullptr → 有武器（剑/枪/拳套等任意类型）
			bHasWeaponFromEquipment =
				EquipmentManager->GetFirstInstanceOfType(
					UDark_TdoreWeaponInstance::StaticClass()) != nullptr;
		}
	}

	// ===== 第 5 步：合并结果 =====
	// 逻辑或：只要任一数据源说"有武器"，就认为有武器
	bHasWeapon = bHasWeaponFromTag || bHasWeaponFromEquipment;

	// ===== 第 6 步：状态变化时打日志 =====
	// 只在变化时输出，避免每帧刷屏
	// bPreviousHasWeapon != bHasWeapon：
	//   true  → 0→1（装备了武器）或 1→0（卸下了武器）
	//   false → 状态没变，跳过
	if (bPreviousHasWeapon != bHasWeapon)
	{
		UE_LOG(LogDark_Tdore, Log,
			TEXT("AnimInstance weapon state changed: AnimInstance=%s Owner=%s HasWeapon=%s ASC=%s FromTag=%s FromEquipment=%s"),
			*GetNameSafe(this),                                // 当前 AnimInstance 名
			*GetNameSafe(GetOwningActor()),                    // 拥有者 Actor
			bHasWeapon ? TEXT("true") : TEXT("false"),         // 新状态
			*GetNameSafe(NativeAbilitySystemComponent.Get()),  // ASC 名（null 时显示 None）
			bHasWeaponFromTag ? TEXT("true") : TEXT("false"),  // 来源1是否匹配
			bHasWeaponFromEquipment ? TEXT("true") : TEXT("false")); // 来源2是否匹配
	}
}

// ============================================================================
// IsDataValid — 编辑器数据验证（仅编辑器构建）
// ============================================================================
#if WITH_EDITOR
EDataValidationResult UDark_TdoreAnimInstance::IsDataValid(FDataValidationContext& Context) const
{
	// 先让父类做基础验证
	Super::IsDataValid(Context);

	// GameplayTagPropertyMap.IsDataValid：
	//   检查 AnimBP Class Defaults 中配的 Tag→变量映射是否有效
	//   例如：Tag 没注册、蓝图变量类型不匹配等
	GameplayTagPropertyMap.IsDataValid(this, Context);

	// Context.GetNumErrors()：
	//   0 → 验证通过
	//  >0 → 有错误，返回 Invalid
	return ((Context.GetNumErrors() > 0)
		? EDataValidationResult::Invalid   // 有错
		: EDataValidationResult::Valid);    // 通过
}
#endif // WITH_EDITOR

// ============================================================================
// NativeInitializeAnimation — AnimInstance 初始化回调（引擎自动调用一次）
// ============================================================================
// 等效于蓝图中的 Event Blueprint Initialize Animation。
// 在 AnimInstance 第一次被使用时调用（通常是 Character 的 Mesh 初始化时）。
//
void UDark_TdoreAnimInstance::NativeInitializeAnimation()
{
	// 先让父类完成基础初始化
	Super::NativeInitializeAnimation();

	// 再次设置 RootMotionMode（防御：防止蓝图中覆盖了构造函数的值）
	RootMotionMode = ERootMotionMode::RootMotionFromMontagesOnly;

	// ===== 缓存 Character 引用 =====
	// GetOwningActor()：AnimInstance 属于哪个 Actor
	// Cast<ACharacter>：必须是 Character（有骨骼网格体）
	// 为什么要缓存？
	//   每帧 NativeUpdateAnimation 都要用 Character，缓存后不用每次 Cast
	NativeCharacter = Cast<ACharacter>(GetOwningActor());

	// ===== 缓存 CharacterMovementComponent =====
	// GetCharacterMovement()：ACharacter 的移动组件
	// 三元运算符：Character 为空 → nullptr；否则取移动组件
	// NativeMovementComponent 是每帧 NativeUpdateAnimation 的核心数据来源
	NativeMovementComponent = NativeCharacter
		? NativeCharacter->GetCharacterMovement()
		: nullptr;

	// ===== 绑定 ASC =====
	// 查找 OwningActor 的 ASC 并调用 InitializeWithAbilitySystem
	if (AActor* OwningActor = GetOwningActor())
	{
		if (UAbilitySystemComponent* ASC =
			UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwningActor))
		{
			// 缓存 ASC + 初始化 GameplayTagPropertyMap（Tag → 蓝图变量映射）
			InitializeWithAbilitySystem(ASC);
		}
	}

	// ===== 首次刷新武器状态 =====
	// 防止 BeginPlay 时角色已经装备了武器，但 bHasWeapon 还是默认 false
	RefreshWeaponState();
}

// ============================================================================
// NativeUpdateAnimation — 每帧动画更新回调（引擎自动调用）
// ============================================================================
// 等效于蓝图中的 Event Blueprint Update Animation。
// 这是动画系统的"心跳"：每帧在这里更新所有 AnimBP 需要的角色状态数据。
//
// 参数 DeltaSeconds：本帧距上一帧的时间（秒），当前版本没用它。
//
void UDark_TdoreAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	// 先让父类更新
	Super::NativeUpdateAnimation(DeltaSeconds);

	// ===== 第 1 步：兜底缓存 Character =====
	// NativeInitializeAnimation 里缓存的 Character 理论上不会变
	// 但如果某种原因（如 Possessed/UnPossessed 切换）导致失效，这里兜底重新 Cast
	if (!NativeCharacter)
	{
		NativeCharacter = Cast<ACharacter>(GetOwningActor());
	}

	// ===== 第 2 步：兜底缓存 MovementComponent =====
	NativeMovementComponent = NativeCharacter
		? NativeCharacter->GetCharacterMovement()  // Character 有效 → 取移动组件
		: nullptr;                                   // Character 空 → 置空

	// ===== 第 3 步：Character 或 MovementComponent 无效 → 全部置零 =====
	// 防御性编程：如果必需的依赖丢失，把所有输出变量归零
	// 这样 AnimBP 读到的是安全的默认值，不会用上一帧的脏数据
	if (!NativeCharacter || !NativeMovementComponent)
	{
		NativeVelocity = FVector::ZeroVector;  // 速度零向量
		NativeGroundSpeed = 0.0f;              // 地面速度 0
		NativeDirection = 0.0f;                // 朝向角 0（正面）
		bNativeShouldMove = false;              // 不在移动
		bNativeIsFalling = false;               // 不在下落
		bHasWeapon = false;                     // 没有武器
		GroundDistance = -1.0f;                 // 离地距离 -1（无效值）
		return;                                 // 提前返回，不执行后续计算
	}

	// ===== 第 4 步：更新武器状态 =====
	// 每帧调用：因为 Tag 可能在任意时刻被 SetLooseGameplayTagCount 修改
	RefreshWeaponState();

	// ===== 第 5 步：速度数据 =====
	// NativeMovementComponent->Velocity：
	//   角色当前的 3D 速度向量（单位：cm/s）
	//   包含 X、Y、Z 三个分量（Z 是垂直速度，跳跃/下落时非零）
	NativeVelocity = NativeMovementComponent->Velocity;

	// Velocity.Size2D()：
	//   取 XY 平面的速度标量（忽略 Z 轴）
	//   为什么忽略 Z？
	//     跳跃/下落时 Z 轴速度很大，如果算入总速度会导致 AnimBP 误播跑步动画
	//     地面移动只关心水平移动速度
	NativeGroundSpeed = NativeVelocity.Size2D();

	// ===== 第 6 步：方向角 =====
	// GetActorRotation().UnrotateVector(Velocity)：
	//   把世界坐标系的速度向量转换到角色的局部坐标系
	//   例如：角色面朝东，实际在往北跑 → 局部速度指向左（-Y）
	const FVector LocalVelocity =
		NativeCharacter->GetActorRotation().UnrotateVector(NativeVelocity);

	// Atan2(LocalVelocity.Y, LocalVelocity.X)：
	//   计算局部速度的方位角（弧度）
	//   X 正轴 = 前方，Y 正轴 = 右方
	//   结果：0=正前，π/2=右，-π/2=左，π/-π=后
	// RadiansToDegrees：弧度 → 度
	const float RawDirection = FMath::RadiansToDegrees(
		FMath::Atan2(LocalVelocity.Y, LocalVelocity.X));

	// ===== 第 7 步：方向角限制（根据移动模式） =====
	// bOrientRotationToMovement = true（锁定移动）：
	//   角色会自动转向移动方向，所以实际方向角不会超过 ±45°
	//   用 Clamp 限制到 -45~45，防止 BlendSpace 误播后退/侧移动画
	//
	// bOrientRotationToMovement = false（自由移动）：
	//   角色可以面朝任意方向移动，方向角可能是 -180~180
	//   直接使用原始值，BlendSpace 需要完整 360° 范围
	NativeDirection = NativeMovementComponent->bOrientRotationToMovement
		? FMath::Clamp(RawDirection, -45.0f, 45.0f)  // 锁定移动：限制 ±45°
		: RawDirection;                                 // 自由移动：完整范围

	// ===== 第 8 步：移动判定 =====
	// ShouldMove 需要同时满足两个条件：
	//   条件1：实际速度 > 阈值（默认 3.0 cm/s）
	//          过滤掉极微小的抖动速度（如动画混合造成的位置偏移）
	//   条件2：有输入加速度（即玩家按了方向键）
	//          GetCurrentAcceleration() 返回当前玩家输入的加速度向量
	//          IsNearlyZero()：加速度接近零向量（没按方向键）
	//
	//   为什么两个条件都要？
	//     如果只看速度：角色被击退时速度很大但不是主动移动
	//     如果只看加速度：按住方向键但撞墙时不应播移动动画
	//     两者 AND 确保：主动移动 + 实际在动
	bNativeShouldMove =
		NativeGroundSpeed > NativeShouldMoveThreshold  // 条件1: 速度 > 3 cm/s
		&& !NativeMovementComponent->GetCurrentAcceleration().IsNearlyZero();  // 条件2: 有输入

	// ===== 第 9 步：下落判定 =====
	// IsFalling()：CharacterMovementComponent 的内置方法
	//   角色不在地面上 → true（跳跃中、掉落中）
	//   AnimBP 用它在"地面移动"和"空中动画"之间切换
	bNativeIsFalling = NativeMovementComponent->IsFalling();

	// ===== 第 10 步：地面距离（FootIK 等后处理用） =====
	// Cast<ADark_TdoreCharacter>：需要用到自定义移动组件的 GetGroundInfo()
	const ADark_TdoreCharacter* Character = Cast<ADark_TdoreCharacter>(GetOwningActor());
	if (!Character)
	{
		return;  // 不是我们的 Character → 取不到地面信息，提前返回
	}

	// CastChecked：确定是 UDark_TdoreCharacterMovementComponent（构造中设置的默认子对象）
	UDark_TdoreCharacterMovementComponent* CharMoveComp =
		CastChecked<UDark_TdoreCharacterMovementComponent>(
			Character->GetCharacterMovement());

	// GetGroundInfo()：返回缓存的地面信息结构体
	//   包含 GroundHitResult（碰撞信息）和 GroundDistance（离地距离）
	//   如果在地面 → GroundDistance = 0
	//   如果在空中 → GroundDistance = 往下 Raycast 到的距离
	const FDark_TdoreCharacterGroundInfo& GroundInfo = CharMoveComp->GetGroundInfo();

	// GroundDistance 给 AnimBP 的 FootIK 节点用
	// 用于 IK 骨骼的偏移量计算，让脚更好地贴合地面
	GroundDistance = GroundInfo.GroundDistance;
}
