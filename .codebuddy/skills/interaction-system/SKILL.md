# 交互系统 (Interaction System) — 完整指南

## Current Project Notes

以下几条以当前项目代码为准，优先级高于本文后面旧示例：

1. 角色默认能力不是直接看某个固定的 `DA_DefaultAbilitySet` 名字，而是看 `PawnData->AbilitySets`。
2. 交互常驻能力的真实 C++ 类是 `Dark_TdoreGameplayAbility_Interact`，授予入口仍然是 `AbilitySet->GiveToAbilitySystem(...)`。
3. GAS 输入 Tag 规范已经统一为 `InputTag.*`，不要再使用旧写法 `Input.Ability.*`。

> 参考 Lyra Interaction/ 目录实现，基于 GAS + 接口模式

---

## 一、架构概览

```
交互 GA (Dark_TdoreGameplayAbility_Interact, OnSpawn)
  └─ 激活时创建 AbilityTask_GrantNearbyInteraction
       ├─ Timer 定时扫描（默认 0.1s）
       ├─ OverlapMultiByChannel(ECC_WorldDynamic, Sphere 500cm)
       ├─ 提取 IInteractableTarget
       │    └─ GatherInteractionOptions → 返回选项列表
       │         ├─ 交互者授权方式: InteractionAbilityToGrant
       │         └─ 目标激活方式: TargetAbilitySystem + Handle
       └─ 授予交互 GA 给 ASC

可交互 Actor (IInteractableTarget 实现者)
  └─ GatherInteractionOptions → 填写 FInteractionOption
       ├─ Text/SubText         ← UI 提示文本
       ├─ InteractionAbilityToGrant ← 授权给交互者的 GA
       └─ InteractionWidgetClass    ← 可选 UI Widget
```

---

## 二、完整调用链路

### 阶段 1：交互 GA 激活（角色生成时）

```
DA_DefaultAbilitySet::GiveToAbilitySystem
  → ASC::GiveAbility(Dark_TdoreGameplayAbility_Interact)
    → ActivationPolicy = OnSpawn → 自动激活

InteractGA::ActivateAbility
  └─ if (ROLE_Authority)
       └─ UAbilityTask_GrantNearbyInteraction::GrantAbilitiesForNearbyInteractors(this, 500cm, 0.1s)
```

### 阶段 2：Timer 定时扫描

```
QueryInteractables()  ← 每 0.1s 调用
  ├─ Step 1: 球形 Overlap (500cm, ECC_WorldDynamic)
  ├─ Step 2: 提取 IInteractableTarget 实现者
  ├─ Step 3: 收集交互选项 → GatherInteractionOptions
  └─ Step 4: 授予交互 GA → ASC::GiveAbility(Spec)
```

### 阶段 3：Single Line Trace 版本（精确瞄准）

```
WaitForInteractableTargets_SingleLineTrace::Activate()
  └─ Timer → 每 0.1s 调用 PerformTrace()
       ├─ AimWithPlayerController: 从摄像机发射射线
       ├─ 命中 → 收集选项 → InteractableObjectsChanged.Broadcast
       └─ bShowDebug: 画调试线（红=命中，绿=未命中）
```

---

## 三、核心数据结构

```
FInteractionQuery                   ← 查询者信息
  ├─ RequestingAvatar (AActor*)
  ├─ RequestingController (AController*)
  └─ OptionalObjectData (UObject*)

FInteractionOptionBuilder          ← 构建器（自动记录 Scope）
  ├─ Scope (IInteractableTarget*)  ← 当前目标
  └─ Options (TArray&)             ← 输出列表

FInteractionOption                 ← 单个交互选项
  ├─ InteractableTarget (引用)
  ├─ Text / SubText (UI 文本)
  ├─ InteractionAbilityToGrant (授予 GA)
  ├─ TargetAbilitySystem + Handle (目标 GA)
  └─ InteractionWidgetClass (UI Widget)
```

---

## 四、两种扫描方式对比

| | GrantNearbyInteraction | WaitForInteractableTargets_SingleLineTrace |
|---|---|---|
| 检测方式 | 球形 Overlap | 摄像机射线 LineTrace |
| 碰撞通道 | ECC_WorldDynamic | 可配置 ProfileName |
| 输出 | 授予 GA 给 ASC | InteractableObjectsChanged Delegate |
| 典型场景 | 靠近自动弹出交互提示（RPG 拾取） | 准心对准才可交互（FPS 瞄准） |

---

## 五、核心概念问答

### 为什么扫描到目标就自动授予 GA？授予给谁？

**授予给交互者（玩家自己），不是目标。** 设计原因：按需授权，而不是常驻。拾取武器的 GA 不应该永远挂在角色身上，只有靠近武器时才临时授予。

GA 授予后一直留在 ASC 上（Lyra 设计选择），`CanActivateAbility` 自然拦截无效激活。

### 授予的 GA 到底有什么作用？

GA **就是交互动作本身**。没有它，靠近武器什么都做不了。

```
完整拾取流程：
  走近武器 → GrantNearbyInteraction 扫描 → 自动授予 BP_GA_PickupWeapon
  → 按 E 键 → ASC 激活 BP_GA_PickupWeapon
  → GA 执行：武器装背包 → 销毁 Actor
```

### 两种交互方式

| 方式 | 适用场景 |
|------|---------|
| 授予 GA (InteractionAbilityToGrant) | 拾取武器 → 玩家获得新能力 |
| 目标 GA (TargetAbilitySystem + Handle) | NPC 对话/商店 → 目标自己执行逻辑 |

---

## 六、文件清单

| 文件 | 作用 |
|------|------|
| `Interaction/Dark_TdoreInteractableTarget.h` | 可交互目标接口 + FInteractionOptionBuilder |
| `Interaction/Dark_TdoreInteractionInstigator.h` | 交互仲裁接口 |
| `Interaction/Dark_TdoreInteractionOption.h` | 交互选项结构体 |
| `Interaction/Dark_TdoreInteractionQuery.h` | 交互查询参数 |
| `Interaction/Dark_TdoreInteractionStatics.h/.cpp` | 提取交互目标的工具函数 |
| `Interaction/Abilities/Dark_TdoreGameplayAbility_Interact.h/.cpp` | OnSpawn 交互 GA |
| `Interaction/Tasks/AbilityTask_GrantNearbyInteraction.h/.cpp` | 球形扫描 + GA 授权 |
| `Interaction/Tasks/AbilityTask_WaitForInteractableTargets.h/.cpp` | 射线检测基类 |
| `Interaction/Tasks/AbilityTask_WaitForInteractableTargets_SingleLineTrace.h/.cpp` | 单线射线交互检测 |
| `Interaction/Dark_TdoreTestInteractable.h/.cpp` | 测试用可交互 Actor |
