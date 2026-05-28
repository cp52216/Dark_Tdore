# GamePhase 游戏阶段系统 — 完整指南

> 参考 Lyra `AbilitySystem/Phases/` 实现，基于 UWorldSubsystem + GameplayAbility

---

## 一、架构概览

```
GameState (Dark_TdoreGameState)
  ├─ ASC (UDark_TdoreAbilitySystemComponent)  ← PhaseAbility 的宿主
  ├─ PostInitializeComponents → InitAbilityActorInfo
  └─ BeginPlay → StartPhase(InitialPhaseClass)

World
  └─ PhaseSubsystem (UDark_TdoreGamePhaseSubsystem)
       ├─ ActivePhaseMap: Handle → {PhaseTag, Callback}
       ├─ PhaseStartObservers / PhaseEndObservers
       ├─ StartPhase → GiveAbilityAndActivateOnce → PhaseAbility 激活
       └─ OnBeginPhase → 取消冲突阶段 + 通知观察者

PhaseAbility (UDark_TdoreGamePhaseAbility)
  ├─ GamePhaseTag: FGameplayTag（如 GamePhase.PreGame）
  ├─ ActivateAbility → Subsystem::OnBeginPhase
  └─ EndAbility → Subsystem::OnEndPhase
```

---

## 二、核心设计原理

### 用 GameplayTag 层级代替状态机

传统方式需要写状态机代码管理阶段切换。GamePhase 系统用 **Tag 的父子层级** 自动处理互斥关系：

```
GamePhase               ← 根
├─ PreGame              ← 父阶段（无子阶段）
├─ WarmUp               ← 父阶段（无子阶段）
├─ Playing              ← 父阶段
│   └─ SuddenDeath      ← 子阶段，兄弟间互斥
└─ PostGame             ← 父阶段（无子阶段）
```

**互斥规则**：
```
A.MatchesTag(B) → return true  含义：A 是 B 的儿子或孙子

Incoming.MatchesTag(Active)     → 保留（儿子/孙子关系）
!Incoming.MatchesTag(Active)    → 取消（非祖先 → 冲突）
```

**示例**：
```
当前活跃: GamePhase.Playing, GamePhase.Playing.WarmUp

启动 GamePhase.Playing.SuddenDeath:
  SuddenDeath.MatchesTag(Playing)?    → Yes → 保留 Playing
  SuddenDeath.MatchesTag(WarmUp)?     → No  → 取消 WarmUp（兄弟互斥）

启动 GamePhase.PostGame:
  PostGame.MatchesTag(Playing)?       → No  → 取消 Playing
  PostGame.MatchesTag(WarmUp)?        → No  → 取消 WarmUp
```

---

## 三、配置清单

### 3.1 GameplayTags（Config/DefaultGameplayTags.ini）

```ini
+GameplayTagList=(Tag="GamePhase.PreGame",DevComment="等待玩家加入阶段")
+GameplayTagList=(Tag="GamePhase.WarmUp",DevComment="热身体验阶段")
+GameplayTagList=(Tag="GamePhase.Playing",DevComment="正式游戏阶段")
+GameplayTagList=(Tag="GamePhase.Playing.SuddenDeath",DevComment="突然死亡子阶段")
+GameplayTagList=(Tag="GamePhase.PostGame",DevComment="游戏结算阶段")
```

### 3.2 GameState 蓝图配置

| 属性 | 位置 | 值 |
|------|------|-----|
| `InitialPhaseClass` | GameState Class Defaults → GamePhase | `BP_Phase_PreGame` |
| GameState Class | GameMode → Class Defaults | `BP_DarkTdoreGameState` |

### 3.3 蓝图阶段能力创建

| 蓝图 | 父类 | GamePhaseTag |
|------|------|-------------|
| `BP_Phase_PreGame` | `Dark_TdoreGamePhaseAbility` | `GamePhase.PreGame` |
| `BP_Phase_WarmUp` | `Dark_TdoreGamePhaseAbility` | `GamePhase.WarmUp` |
| `BP_Phase_Playing` | `Dark_TdoreGamePhaseAbility` | `GamePhase.Playing` |
| `BP_Phase_PostGame` | `Dark_TdoreGamePhaseAbility` | `GamePhase.PostGame` |

---

## 四、完整调用链路

### 阶段 1：初始化 — GameState 启动

```
GameState::PostInitializeComponents()
  └─ ASC->InitAbilityActorInfo(Owner=this, Avatar=this)
       → ASC 就绪，可以接收 PhaseAbility

GameState::BeginPlay()
  ├─ HasAuthority() && InitialPhaseClass ?
  │    ├─ GetWorld()->GetSubsystem<PhaseSubsystem>()
  │    └─ PhaseSubsystem->StartPhase(InitialPhaseClass)
  │         ↓
  │    PhaseSubsystem::StartPhase(BP_Phase_PreGame)
  │
  └─ 失败 → 日志: "[GameState::BeginPlay] 阶段未启动"
```

### 阶段 2：StartPhase — 阶段能力授予与激活

```
PhaseSubsystem::StartPhase(PhaseAbility, PhaseEndedCallback)
  │
  ├─ Step 1: 从 GameState 获取 ASC
  │    GameState_ASC = World->GetGameState()->FindComponentByClass<UDask_TdoreAbilitySystemComponent>()
  │    if (!GameState_ASC) → ensure 失败，return
  │
  ├─ Step 2: 创建 AbilitySpec → GiveAbilityAndActivateOnce
  │    FGameplayAbilitySpec PhaseSpec(PhaseAbility, 1, 0, this)
  │    SpecHandle = GameState_ASC->GiveAbilityAndActivateOnce(PhaseSpec)
  │
  │    GiveAbilityAndActivateOnce 内部：
  │      → GiveAbility(PhaseSpec)       ← 授予（或使用已有 Spec）
  │      → TryActivateAbility(Handle)   ← 立即激活
  │        → CanActivateAbility()       ← 检查 BlockedTags/Cost
  │        → 通过 → ActivateAbility()   ← 进入 PhaseAbility
  │
  ├─ Step 3: 校验激活成功
  │    FoundSpec = ASC->FindAbilitySpecFromHandle(SpecHandle)
  │    if (FoundSpec && FoundSpec->IsActive())
  │      → ActivePhaseMap.Add(Handle, {PhaseEndedCallback})
  │    else
  │      → PhaseEndedCallback(nullptr)   ← 激活失败回调
```

### 阶段 3：PhaseAbility 的生命周期

```
PhaseAbility::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData)
  │
  ├─ if (IsNetAuthority())
  │    ├─ PhaseSubsystem = UWorld::GetSubsystem<UDark_TdoreGamePhaseSubsystem>(World)
  │    └─ PhaseSubsystem->OnBeginPhase(this, Handle)
  │         ↓
  │    OnBeginPhase(PhaseAbility, Handle):
  │      ├─ IncomingTag = PhaseAbility->GetGamePhaseTag()   // "GamePhase.PreGame"
  │      ├─ 遍历 ActivePhaseMap → 取消冲突阶段
  │      │    for (ActivePhase : ActivePhaseMap)
  │      │      if (!IncomingTag.MatchesTag(ActivePhase.Tag))
  │      │        → ASC->CancelAbilityHandle(ActivePhase.Handle)  // 取消冲突阶段
  │      ├─ ActivePhaseMap.Add(Handle, {IncomingTag})
  │      └─ 通知 PhaseStartObservers
  │           for (Observer : PhaseStartObservers)
  │             if (Observer.IsMatch(IncomingTag))
  │               → Observer.PhaseCallback(IncomingTag)
  │
  └─ Super::ActivateAbility(...)   ← 蓝图可覆写添加阶段逻辑（如倒计时、等待玩家）

─────────────────────────────────────────────

PhaseAbility::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled)
  │
  ├─ if (IsNetAuthority())
  │    ├─ PhaseSubsystem = UWorld::GetSubsystem<UDark_TdoreGamePhaseSubsystem>(World)
  │    └─ PhaseSubsystem->OnEndPhase(this, Handle)
  │         ↓
  │    OnEndPhase(PhaseAbility, Handle):
  │      ├─ PhaseEndedCallback(PhaseAbility)    ← 执行 StartPhase 时传入的回调
  │      ├─ ActivePhaseMap.Remove(Handle)
  │      └─ 通知 PhaseEndObservers
  │           for (Observer : PhaseEndObservers)
  │             if (Observer.IsMatch(EndedTag))
  │               → Observer.PhaseCallback(EndedTag)
  │
  └─ Super::EndAbility(...)
```

---

## 五、关键函数详解

### UDark_TdoreGamePhaseSubsystem

| 函数 | 作用 | 调用者 |
|------|------|--------|
| `StartPhase(PhaseClass, Callback)` | 授予+激活阶段能力 | GameState::BeginPlay / 蓝图 |
| `OnBeginPhase(Ability, Handle)` | 取消冲突阶段 + 通知观察者 | PhaseAbility::ActivateAbility |
| `OnEndPhase(Ability, Handle)` | 执行回调 + 通知观察者 | PhaseAbility::EndAbility |
| `WhenPhaseStartsOrIsActive(Tag, ...)` | 注册阶段启动监听 | 任意系统 |
| `WhenPhaseEnds(Tag, ...)` | 注册阶段结束监听 | 任意系统 |
| `IsPhaseActive(Tag)` | 查询阶段是否活跃 | 蓝图/逻辑判断 |

### UDark_TdoreGamePhaseAbility

| 函数 | 作用 |
|------|------|
| `ActivateAbility` | 通知 Subsystem → OnBeginPhase |
| `EndAbility` | 通知 Subsystem → OnEndPhase |
| `GetGamePhaseTag()` | 返回配置的 GamePhaseTag |

### FPhaseObserver::IsMatch

```cpp
ExactMatch:   ComparePhaseTag == PhaseTag      // 精确匹配
PartialMatch: ComparePhaseTag.MatchesTag(PhaseTag)  // 层级匹配
```

---

## 六、互斥规则表

| 当前活跃 | 新启动 | 结果 | 原因 |
|----------|--------|------|------|
| PreGame | Playing | PreGame 被取消 | Playing 不匹配 PreGame |
| Playing | SuddenDeath | 两者共存 | SuddenDeath 匹配 Playing（父子） |
| WarmUp | Playing | WarmUp 被取消 | Playing 不匹配 WarmUp |
| Playing+SuddenDeath | PostGame | 全部被取消 | PostGame 不匹配 Playing |

---

## 七、日志输出参考

```
[GameState::BeginPlay] HasAuthority=1, InitialPhaseClass=BP_Phase_PreGame_C, PhaseSubsystem=Dark_TdoreGamePhaseSubsystem
[GameState::BeginPlay] 启动阶段: BP_Phase_PreGame_C
[GamePhase] 开始阶段: 'GamePhase.PreGame' (BP_Phase_PreGame_C)

-- 后续切换到 Playing --
[GamePhase] 开始阶段: 'GamePhase.Playing' (BP_Phase_Playing_C)
[GamePhase]   → 结束冲突阶段: 'GamePhase.PreGame' (BP_Phase_PreGame_C)
[GamePhase] 结束阶段: 'GamePhase.PreGame' (BP_Phase_PreGame_C)
```

---

## 八、文件清单

| 文件 | 作用 |
|------|------|
| `AbilitySystem/Phases/Dark_TdoreGamePhaseLog.h` | 日志类别声明 |
| `AbilitySystem/Phases/Dark_TdoreGamePhaseAbility.h/.cpp` | 阶段能力基类（GA） |
| `AbilitySystem/Phases/Dark_TdoreGamePhaseSubsystem.h/.cpp` | 阶段管理子系统（WorldSubsystem） |
| `GameModes/Dark_TdoreGameState.h/.cpp` | GameState：ASC 宿主 + BeginPlay 启动初始阶段 |
| `Config/DefaultGameplayTags.ini` | GamePhase.* 标签定义 |

---

## 九、扩展用法

### 9.1 监听阶段切换

```cpp
// C++ 中监听 Playing 阶段
PhaseSubsystem->WhenPhaseStartsOrIsActive(
    FGameplayTag::RequestGameplayTag("GamePhase.Playing"),
    EPhaseTagMatchType::ExactMatch,
    FDarkTdoreGamePhaseTagDelegate::CreateLambda([](const FGameplayTag& Tag) {
        // Playing 阶段开始了，启用战斗输入
    }));
```

### 9.2 阶段结束时收尾

```cpp
PhaseSubsystem->WhenPhaseEnds(
    FGameplayTag::RequestGameplayTag("GamePhase.Playing"),
    EPhaseTagMatchType::PartialMatch,
    FDarkTdoreGamePhaseTagDelegate::CreateLambda([](const FGameplayTag& Tag) {
        // Playing 结束（切换 PostGame），显示结算 UI
    }));
```

### 9.3 在 PhaseAbility 蓝图中加逻辑

```
BP_Phase_PreGame → EventGraph → ActivateAbility
  → SetTimer(5s) → StartPhase(BP_Phase_Playing)  // 5 秒倒计时后进入 Playing
```

### 9.4 条件触发阶段

```cpp
// 所有玩家就绪 → 从 PreGame 切换到 Playing
PhaseSubsystem->StartPhase(UDark_TdoreGamePhaseAbility::Phase_Playing);
// PreGame 自动结束，Playing 开始
```
