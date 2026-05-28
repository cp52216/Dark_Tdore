# 交互系统 (Interaction System) — 完整指南

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

## 二、配置清单

### 2.1 DA_DefaultAbilitySet

```
DA_DefaultAbilitySet → GrantedAbilities:
  + Dark_TdoreGameplayAbility_Interact  (OnSpawn 自动激活)
```

### 2.2 可交互目标 Actor

```
任意 Actor 实现 IInteractableTarget 接口：
  GatherInteractionOptions:
    Option.Text = "拾取武器"
    Option.InteractionAbilityToGrant = BP_GA_PickupWeapon
    OptionBuilder.AddInteractionOption(Option)
```

---

## 三、完整调用链路

### 阶段 1：交互 GA 激活（角色生成时）

```
DA_DefaultAbilitySet::GiveToAbilitySystem
  → ASC::GiveAbility(Dark_TdoreGameplayAbility_Interact)
    → ActivationPolicy = OnSpawn → 自动激活

InteractGA::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData)
  │
  ├─ Super::ActivateAbility(...)
  │
  └─ if (ROLE_Authority)  ← 仅服务器执行
       │
       ├─ UE_LOG: "[Interaction] 交互 GA 已激活: BP_ThirdPersonCharacter_C_0"
       │
       └─ UAbilityTask_GrantNearbyInteraction::GrantAbilitiesForNearbyInteractors(this, 500cm, 0.1s)
            └─ Task->ReadyForActivation()
```

### 阶段 2：GrantNearbyInteraction — Timer 定时扫描

```
AbilityTask_GrantNearbyInteraction::Activate()
  │
  └─ World->GetTimerManager().SetTimer(QueryTimerHandle, 0.1s, true)
       │
       ▼ 每 0.1s 调用
QueryInteractables()
  │
  ├─ Step 1: 球形 Overlap
  │    World->OverlapMultiByChannel(Out OverlapResults,
  │      ActorOwner->GetActorLocation(), FQuat::Identity,
  │      ECC_WorldDynamic, FCollisionShape::MakeSphere(500cm), Params)
  │
  │    → 返回 500cm 范围内所有设置了 WorldDynamic 碰撞响应的 Actor
  │    → 如果没有 Overlap → return（跳过后续）
  │
  ├─ Step 2: 提取交互目标
  │    AppendInteractableTargetsFromOverlapResults(OverlapResults, Out InteractableTargets)
  │    → 遍历每个 OverlapResult
  │    → 检查 Actor 是否实现 IInteractableTarget ✓ → 加入列表
  │    → 检查 Component 是否实现 IInteractableTarget ✓ → 加入列表
  │
  │    → 如果没有可交互目标 → return（跳过后续）
  │
  ├─ Step 3: 收集交互选项
  │    for (InteractiveTarget : InteractableTargets)
  │    {
  │        FInteractionOptionBuilder Builder(InteractiveTarget, Options);
  │        InteractiveTarget->GatherInteractionOptions(Query, Builder);
  │        // ↑ 目标填充 Text、InteractionAbilityToGrant 等
  │    }
  │
  └─ Step 4: 授予交互 GA
       for (Option : Options)
       {
           if (Option.InteractionAbilityToGrant)
           {
               if (!InteractionAbilityCache.Contains(GA_Class))
               {
                   ASC->GiveAbility(Spec);                  // 授予 GA
                   InteractionAbilityCache.Add(GA_Class, Handle);  // 缓存（避免重复授予）
                   UE_LOG: "[Interaction] 授予交互 GA: BP_GA_PickupWeapon"
               }
           }
       }
```

### 阶段 3：可交互目标收集选项

```
Dark_TdoreTestInteractable::GatherInteractionOptions(Query, Builder)
  │
  ├─ 创建 FDark_TdoreInteractionOption Option
  ├─ Option.Text = "拾取测试物品"
  ├─ Option.InteractionAbilityToGrant = BP_GA_PickupTest  (可为 null)
  │
  ├─ Builder.AddInteractionOption(Option)
  │    → Option.InteractableTarget = this (Builder 自动设置)
  │    → Options.Add(Option)
  │
  └─ UE_LOG: "[Interaction] TestInteractable 'XXX' 被扫描到，返回选项: '拾取测试物品'"
```

### 阶段 4：Single Line Trace 版本（精确瞄准）

```
WaitForInteractableTargets_SingleLineTrace::Activate()
  │
  └─ Timer → 每 0.1s 调用 PerformTrace()

PerformTrace()
  │
  ├─ AimWithPlayerController: 从摄像机发射射线
  │    → PC->GetPlayerViewPoint()  获取摄像机位置/方向
  │    → LineTrace 从摄像机到前方 MaxRange 距离
  │
  ├─ 命中 → AppendInteractableTargetsFromHitResult
  │
  ├─ UpdateInteractableOptions:
  │    → 收集选项 → 过滤可激活的 → 与上次比较
  │    → 如果变化 → InteractableObjectsChanged.Broadcast(NewOptions)
  │
  └─ bShowDebug: 画调试线（红=命中，绿=未命中）
```

---

## 四、核心数据结构关系

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

## 五、两种扫描方式对比

| | GrantNearbyInteraction | WaitForInteractableTargets_SingleLineTrace |
|---|---|---|
| 检测方式 | 球形 Overlap | 摄像机射线 LineTrace |
| 碰撞通道 | ECC_WorldDynamic | 可配置 ProfileName |
| 输出 | 授予 GA 给 ASC | InteractableObjectsChanged Delegate |
| 典型场景 | 靠近自动弹出交互提示（RPG 拾取） | 准心对准才可交互（FPS 瞄准） |
| 可交互目标要求 | 有碰撞体 + WorldDynamic 响应 | 有碰撞体 + 对应 Profile 响应 |

---

## 六、文件清单

| 文件 | 作用 |
|------|------|
| `Interaction/Dark_TdoreInteractableTarget.h` | 可交互目标接口 + FInteractionOptionBuilder |
| `Interaction/Dark_TdoreInteractionInstigator.h` | 交互仲裁接口 |
| `Interaction/Dark_TdoreInteractionOption.h` | 交互选项结构体 |
| `Interaction/Dark_TdoreInteractionQuery.h` | 交互查询参数 |
| `Interaction/Dark_TdoreInteractionStatics.h/.cpp` | 提取交互目标的工具函数 |
| `Interaction/Dark_TdoreInteractionDurationMessage.h` | 交互持续时间消息体 |
| `Interaction/Abilities/Dark_TdoreGameplayAbility_Interact.h/.cpp` | OnSpawn 交互 GA |
| `Interaction/Tasks/AbilityTask_GrantNearbyInteraction.h/.cpp` | 球形扫描 + GA 授权 |
| `Interaction/Tasks/AbilityTask_WaitForInteractableTargets.h/.cpp` | 射线检测基类（LineTrace/瞄准） |
| `Interaction/Tasks/AbilityTask_WaitForInteractableTargets_SingleLineTrace.h/.cpp` | 单线射线交互检测 |
| `Interaction/Dark_TdoreTestInteractable.h/.cpp` | 测试用可交互 Actor |

---

## 七、测试验证

### 快速验证（已配好）

1. `DA_DefaultAbilitySet` → 添加 `Dark_TdoreGameplayAbility_Interact`
2. 关卡中拖入 `Dark_TdoreTestInteractable`（靠角色出生点）
3. 测试 Actor 需要 SphereCollision 设置为 ECC_WorldDynamic（C++ 已配好）
4. PIE → 日志：

```
[Interaction] 交互 GA 已激活: BP_ThirdPersonCharacter_C_0 (范围=500cm, 频率=0.10s)
[Interaction] TestInteractable 'Dark_TdoreTestInteractable_xxx' 被扫描到，返回选项: '拾取测试物品'
[Interaction] 授予交互 GA: BP_GA_PickupTest → AbilitySystemComponent
```

### 扩展测试

创建 `BP_GA_Pickup` → 蓝图实现拾取逻辑 → 挂到 `TestInteractable::InteractionAbilityClass`。

---

## 八、核心概念问答

### 8.1 为什么扫描到目标就自动授予 GA？授予给谁？

**授予给交互者（玩家自己），不是目标。**

```
选项里有 InteractionAbilityToGrant != null
  → GrantNearbyInteraction 调用:
      AbilitySystemComponent->GiveAbility(Spec)
      // ↑ 这是角色自己的 ASC，不是目标的 ASC
```

设计原因：**按需授权，而不是常驻**。拾取武器的 GA 不应该永远挂在角色身上，只有靠近武器时才临时授予。离开后 GA 仍在 ASC 上但 `CanActivateAbility` 会自然拦截（目标不在范围内）。

```
你走近武器 → 扫描到 → 授予 BP_GA_PickupWeapon → 按 E 激活 → 拾取
你离开武器 → GA 还在 → 按 E → CanActivateAbility 返回 false → 无效
```

### 8.2 授予的 GA 离开/销毁后会自动消失吗？

**不会。** GA 授予后一直留在 ASC 上。这是 Lyra 的设计选择：

- 不主动移除 — 避免 GiveAbility/RemoveAbility 的网络复制开销
- 下次靠近同一类武器时缓存命中，不需要重新授予
- `CanActivateAbility` 自然拦截无效激活

如果你需要"离开即移除"，可在 Timer 里加反向扫描逻辑。

### 8.3 授予的 GA 到底有什么作用？

GA **就是交互动作本身**。没有它，靠近武器什么都做不了。

```
完整拾取流程：
  走近武器（5m内）
  → GrantNearbyInteraction 扫描 → 自动授予 BP_GA_PickupWeapon
  → 按 E 键 → ASC 激活 BP_GA_PickupWeapon
  → GA 执行：武器装背包 → 销毁 Actor → 再授予 BP_GA_Fire

授予前：ASC 上只有 GA_TestQ, GA_Death
授予后：ASC 上多了 BP_GA_PickupWeapon
```

### 8.4 为什么目标说"授予这个 GA"，扫描端就照做？

这是交互选项的协议。目标在 `GatherInteractionOptions` 里声明自己的交互方式：

```cpp
// 武器说：
Option.InteractionAbilityToGrant = BP_GA_PickupWeapon;  // "让玩家获得拾取能力"

// 门说：
Option.TargetAbilitySystem = 门ASC;
Option.TargetInteractionAbilityHandle = OpenDoorHandle;  // "在我身上激活开门能力"
```

扫描端不判断，只是忠实地执行目标声明的内容。

### 8.5 场景里很多 NPC，每个要单独创建 GA 吗？

**不需要。** 两种复用方式：

**方式一（授予 GA — 适合拾取）**：多个目标共享同一 GA 类，通过 Payload 传上下文：
```
3 个不同武器都返回 {InteractionAbilityToGrant = GA_PickupWeapon}
  → 玩家 ASC 上只有一份 GA_PickupWeapon
  → 激活时 Payload.Target 指向具体哪个武器
```

**方式二（目标 GA — 适合 NPC 对话/商店）**：每个 NPC 有自己的 ASC 和 GA 实例：
```
3 个 NPC 都配了 GA_Dialogue（各自独立实例）
  → 交互选项设置 TargetAbilitySystem + TargetInteractionAbilityHandle
  → 按键触发 → TriggerAbilityFromGameplayEvent → 激活 NPC 自己的 GA
  → NPC 的 GA 天然知道自己是谁
```

| 方式 | 适用场景 |
|------|---------|
| 授予 GA (InteractionAbilityToGrant) | 拾取武器 → 玩家获得新能力 |
| 目标 GA (TargetAbilitySystem + Handle) | NPC 对话/商店 → 目标自己执行逻辑 |

### 8.6 NPC 对话如何触发？

```
NPC::GatherInteractionOptions:
  Option.Text = "对话";
  Option.TargetAbilitySystem = NPCAsc;           // NPC 自己的 ASC
  Option.TargetInteractionAbilityHandle = Handle; // 提前授予的 GA_Dialogue Handle
  Builder.AddOption(Option);

玩家走近 NPC → 扫描到 → 存入 CurrentOptions
按交互键 → TriggerInteraction:
  → NPC_ASC->TriggerAbilityFromGameplayEvent(DialogueHandle, Interact_Activate, Payload)
  → NPC 的 GA_Dialogue::ActivateAbility:
        Payload.Target = Player → "谁在跟我对话"
        → 打开对话 UI / 朝向玩家
```

### 8.7 FInteractionQuery 的作用是什么？

**查询者身份的通行证。** 每个交互目标在 `GatherInteractionOptions` 里拿到它，可以根据请求者返回不同选项。

```cpp
// GrantNearbyInteraction 构建 Query:
InteractionQuery.RequestingAvatar = ActorOwner;          // 玩家 Pawn
InteractionQuery.RequestingController = Cast<AController>(...); // 控制器

// 传给目标:
InteractiveTarget->GatherInteractionOptions(InteractionQuery, Builder);

// 目标内部根据 Query 做判断:
void ADoor::GatherInteractionOptions(const FInteractionQuery& Query, FInteractionOptionBuilder& Builder)
{
    AActor* Player = Query.RequestingAvatar.Get();  // 谁在问？
    if (Player && HasKeyFor(Player))
        Option.Text = "开门";
    else
        Option.Text = "需要钥匙";  // 不授予 GA
}
```

| 字段 | 来源 | 用途 |
|------|------|------|
| `RequestingAvatar` | `GetAvatarActor()` | 判断距离、等级、是否持有钥匙 |
| `RequestingController` | `ActorOwner->GetOwner()` | 区分 PlayerController vs AIController |
| `OptionalObjectData` | (当前未填) | 传额外数据（背包组件、任务组件） |

### 8.8 TScriptInterface<IInteractableTarget>(Actor*) 为什么可以这样构造？

这不是接口的构造函数，是 **`TScriptInterface<T>` 的隐式构造**——UE 引擎能力：

```cpp
TScriptInterface<IDark_TdoreInteractableTarget> InteractableActor(Overlap.GetActor());
//                ↑                                 ↑              ↑
//              模板类型                         变量名          AActor* → UObject*

// 引擎内部（伪代码）：
TScriptInterface(UObject* Object) {
    if (Object && Object->ImplementsInterface(UMyInterface::StaticClass())) {
        ObjectPointer = Object;           // 有效
        InterfacePointer = ...;            // 有效
    }
    // 没实现接口 → 两个指针都是 nullptr
}

// 判空即判断是否实现了接口：
if (InteractableActor)  // ← Actor 实现了 IInteractableTarget ✅
    InteractableActor->GatherInteractionOptions(...);
else                    // ← 没实现，TScriptInterface 为 null
```

**一行代码同时完成「类型转换 + 接口检查」。** 这是 UE 反射系统对 `TScriptInterface` 的特别设计。任何 `UObject*` 都可以塞进去，引擎自动判断是否实现了对应接口。

---

## 九、已知限制与改进方向

### 9.1 GA 离开范围后不会自动撤销（和 Lyra 保持一致）

**现状：** GrantNearbyInteraction 只负责授予 GA，不负责撤销。离开目标 5m 范围后 GA 仍留在 ASC 上。

```
你走进 5m → 扫描到 → ASC::GiveAbility(BP_GA_PickupWeapon)
你走出 5m → 扫描不到 → 不重复授予 → 但 GA 还在 ASC 上
```

**Lyra 的设计选择：** GA 永不撤销，依赖以下机制兜底：
1. `WaitForInteractableTargets` 实时更新 `CurrentOptions`（射线扫描）
2. `TriggerInteraction` 使用 `CurrentOptions[0]`（始终是最新的扫描结果）
3. `UpdateInteractableOptions` 只广播 `CanActivateAbility` 通过的选项

**结论：** 和 Lyra 保持一致，不做自动撤销。如需要可后续按场景添加 `CanActivateAbility` 检查。

### 9.2 碰撞通道硬编码为 ECC_WorldDynamic

当前 GrantNearbyInteraction 的 Overlap 通道固定为 `ECC_WorldDynamic`，无法通过蓝图配置。后续可改为 `UPROPERTY(EditDefaultsOnly)` 暴露给蓝图。

### 9.3 SingleLineTrace 未接入当前项目

`AbilityTask_WaitForInteractableTargets_SingleLineTrace` 已完整实现但未被使用。当前项目仅使用 `GrantNearbyInteraction`（OnSpawn 球形扫描）。如果需要精确瞄准交互（FPS 准心检测），在对应的 GA 中创建 SingleLineTrace Task 并绑定 Delegate 即可。
