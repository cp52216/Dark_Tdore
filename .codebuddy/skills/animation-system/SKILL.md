# 动画分层系统指南

## 概述

Dark_Tdore 的动画系统采用 **C++ 数据桥接 + 蓝图 Linked Anim Layer 分层** 架构。
核心原则：走跑移动由 CharacterMovementComponent 驱动，技能动画由 Montage RootMotion 驱动，武器动画通过 Linked Anim Layer 动态切换。

## 整体架构

```
UDark_TdoreAnimInstance (C++ 基类)
├── 数据桥接：GameplayTagPropertyMap, GroundSpeed, Direction, bHasWeapon
├── RootMotionMode = RootMotionFromMontagesOnly
│
├── ABP_Character_Base (主动画蓝图)
│   ├── 空手移动状态机 (BS_Idle_Walk_Run)
│   ├── Blend Poses by Bool → bHasWeapon
│   │   ├── false → 空手动画
│   │   └── true  → Linked Anim Layer (ALI_ItemAnimLayers)
│   │
│   └── Linked Anim Layer 动态路由
│       ├── ABP_ItemAnimLayers_Sword  (直接继承 Dark_TdoreAnimInstance)
│       └── ABP_ItemAnimLayers_Gloves (直接继承 Dark_TdoreAnimInstance)
```

## 第一层：C++ 数据桥接 (`UDark_TdoreAnimInstance`)

**位置**: `Source/Dark_Tdore/Animation/Dark_TdoreAnimInstance.h/.cpp`

每帧通过 `NativeUpdateAnimation` 计算并暴露以下数据给蓝图：

| 变量 | 类型 | 说明 |
|------|------|------|
| `NativeCharacter` | `ACharacter*` | 缓存角色引用，避免每帧 Cast |
| `NativeMovementComponent` | `UCharacterMovementComponent*` | 缓存的移动组件 |
| `NativeVelocity` | `FVector` | 当前速度向量 |
| `NativeGroundSpeed` | `float` | XY 平面速度，供 BlendSpace Speed 轴使用 |
| `NativeDirection` | `float` | 速度相对角色朝向，度（0=前，90=右，-90=左，180/-180=后） |
| `bNativeShouldMove` | `bool` | 速度 > 3 且有加速度输入 |
| `bNativeIsFalling` | `bool` | 是否空中 |
| `bHasWeapon` | `bool` | 是否装备武器（ASC Tag + Equipment 双重检测） |
| `GroundDistance` | `float` | 离地距离，供 FootIK 等后处理 |

### RootMotion 策略

```cpp
RootMotionMode = ERootMotionMode::RootMotionFromMontagesOnly;
```

- 走跑移动 → CMC 驱动位移，动画 In-Place
- 技能 Montage → RootMotion 驱动位移
- 八方向 BlendSpace 动画全部 `bEnableRootMotion = false`

### 武器状态检测（双重来源）

```cpp
// 来源1: ASC 拥有 Status.Weapon.Equipped GameplayTag
// 来源2: EquipmentManagerComponent 中存在 WeaponInstance
bHasWeapon = bHasWeaponFromTag || bHasWeaponFromEquipment;
```

## 第二层：ABP_Character_Base 状态机分支

**位置**: `Content/ThirdPerson/Animations/ABP_Character_Base.uasset`

AnimGraph 核心链路：

```
NativeUpdateAnimation (数据计算)
    ↓
NativeGroundSpeed + NativeDirection → BS_Idle_Walk_Run (八方向 BlendSpace)
    ↓
bHasWeapon? → Blend Poses by Bool
    ├── false → 空手动画 (直接输出)
    └── true  → Linked Anim Layer 节点 → 路由到武器 AnimBP
```

### BS_Idle_Walk_Run BlendSpace

- X 轴：Direction (-180° ~ 180°, 8 格)
- Y 轴：Speed (0 ~ 600)
- Walk 动画 @ Y=300，Jog 动画 @ Y=600，Idle @ Y=0
- `AxisToScaleAnimation = None` — 不自动缩放播放速率

## 第三层：武器动画层 (Linked Anim Layer)

### ALI_ItemAnimLayers（动画层接口）

**位置**: `Content/ThirdPerson/Animations/LinkedLayers/ALI_ItemAnimLayers.uasset`

定义武器动画蓝图必须实现的功能接口。

### ABP_ItemAnimLayers_Sword

**位置**: `Content/ThirdPerson/Animations/LinkedLayers/ABP_ItemAnimLayers_Sword.uasset`

- **继承**: `UDark_TdoreAnimInstance`（直接继承，非继承 Base）
- **实现**: `ALI_ItemAnimLayers` 接口
- **骨架**: `SK_Mannequin`
- **动画来源**: `Content/WuDang_Montage/Animation/Adapted/`

Sword 中包含的动画状态机：

| 状态 | 数据来源 | 动画资源 |
|------|----------|---------|
| 八方向走跑 | WuDang BlendSpace | Move/Front,Back,Left,Right |
| 闪避 (Dodge) | Dodge/ | avoid_front, roll_front 等 |
| 受击 (Hit) | Hit/ | hit_front, hit_left 等 + stun |
| 连击 (Combo) | Combos/ | Combo01_1 ~ Combo01_4 |

### 继承关系说明（重要）

```
UAnimInstance
└── UDark_TdoreAnimInstance (C++ 基类: 数据桥接)
    ├── ABP_Character_Base
    └── ABP_ItemAnimLayers_Sword      ← ✅ 直接继承
    └── ABP_ItemAnimLayers_Gloves     ← ✅ 直接继承
```

Sword/Gloves 继承 Base 时，Base 的未实现接口会导致武器动画层不生效。
**改为直接继承 `UDark_TdoreAnimInstance` 后正常。**

## 完整数据流

```
CharacterMovementComponent.Velocity
    ↓
UDark_TdoreAnimInstance::NativeUpdateAnimation (C++)
    ↓ 计算并缓存
NativeGroundSpeed, NativeDirection, bHasWeapon, ...
    ↓ (蓝图可读)
ABP_Character_Base::EventGraph
    ↓ 传入
AnimGraph:
    BS_Idle_Walk_Run (X=Direction, Y=GroundSpeed)
    ↓
    bHasWeapon? → BlendPosesByBool
        ↓ true
        Linked Anim Layer 节点
        ↓ 动态路由
        ABP_ItemAnimLayers_Sword
        ↓
        Sword BlendSpace + 状态机
        ↓
        最终输出姿态
```

## 八方向移动动画配置要点

### 空手 (BS_Idle_Walk_Run)
- Speed 轴范围：0 ~ 600
- Walk 样本 @ Y=300，Jog @ Y=600
- MaxWalkSpeed = 500，动画自然落在两者之间混合

### 剑 (WuDang BlendSpace)
- Speed 轴必须与 CMC MaxWalkSpeed 对齐
- 如果轴范围只有 0~100，Speed=500 会被 clamp 到 100，动画播放速率错误
- 正确配置：轴范围 0~600，样本值按动画实际速度放置

### 常见问题

| 现象 | 原因 | 解决 |
|------|------|------|
| 动画走得飞快 | BlendSpace Speed 轴范围太小（如 0~100），实际 Speed=500 被 clamp | 扩展轴范围到 0~600 |
| 角色拉回 | 骨架不匹配，不同动画资源用不同 Skeleton | 统一使用 `SK_Mannequin` |
| 动画抖动 | `ShouldMove` 阈值太低 | `NativeShouldMoveThreshold = 3.0` |

## 相关文件清单

| 文件 | 路径 | 职责 |
|------|------|------|
| `Dark_TdoreAnimInstance.h/.cpp` | `Source/Dark_Tdore/Animation/` | C++ 数据桥接基类 |
| `ABP_Character_Base.uasset` | `Content/ThirdPerson/Animations/` | 主动画蓝图 |
| `ALI_ItemAnimLayers.uasset` | `Content/ThirdPerson/Animations/LinkedLayers/` | 动画层接口 |
| `ABP_ItemAnimLayers_Sword.uasset` | `Content/ThirdPerson/Animations/LinkedLayers/` | 剑动画层 |
| `ABP_ItemAnimLayers_Gloves.uasset` | `Content/ThirdPerson/Animations/LinkedLayers/` | 拳套动画层 |
| `BS_Idle_Walk_Run.uasset` | `Content/Characters/Mannequins/Anims/Unarmed/` | 空手 BlendSpace |
| `BlendSpace.uasset` | `Content/WuDang_Montage/AdvancedAsset/BlendSpace/` | 剑 BlendSpace |
| Move 动画 | `Content/WuDang_Montage/Animation/Adapted/Move/` | 剑八方向位移 |
| Dodge 动画 | `Content/WuDang_Montage/Animation/Adapted/Dodge/` | 剑闪避 |
| Hit 动画 | `Content/WuDang_Montage/Animation/Adapted/Hit/` | 剑受击 |
| Combo 动画 | `Content/WuDang_Montage/Animation/Adapted/Combos/` | 剑连击 |
