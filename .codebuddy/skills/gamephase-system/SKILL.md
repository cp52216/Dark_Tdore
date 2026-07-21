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
- `Incoming.MatchesTag(Active)` → 保留（儿子/孙子关系）
- `!Incoming.MatchesTag(Active)` → 取消（非祖先 → 冲突）

---

## 三、配置清单

### GameplayTags（Config/DefaultGameplayTags.ini）

```ini
+GameplayTagList=(Tag="GamePhase.PreGame",DevComment="等待玩家加入阶段")
+GameplayTagList=(Tag="GamePhase.WarmUp",DevComment="热身体验阶段")
+GameplayTagList=(Tag="GamePhase.Playing",DevComment="正式游戏阶段")
+GameplayTagList=(Tag="GamePhase.Playing.SuddenDeath",DevComment="突然死亡子阶段")
+GameplayTagList=(Tag="GamePhase.PostGame",DevComment="游戏结算阶段")
```

### 蓝图阶段能力创建

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

GameState::BeginPlay()
  └─ HasAuthority() && InitialPhaseClass ?
       └─ PhaseSubsystem->StartPhase(InitialPhaseClass)
```

### 阶段 2：StartPhase — 阶段能力授予与激活

```
PhaseSubsystem::StartPhase(PhaseAbility, PhaseEndedCallback)
  ├─ Step 1: 从 GameState 获取 ASC
  ├─ Step 2: GiveAbilityAndActivateOnce → 授予+立即激活
  └─ Step 3: 校验激活成功 → ActivePhaseMap.Add(Handle, {Callback})
```

### 阶段 3：PhaseAbility 的生命周期

```
PhaseAbility::ActivateAbility
  └─ Subsystem->OnBeginPhase(this, Handle):
       ├─ 遍历 ActivePhaseMap → 取消冲突阶段
       ├─ ActivePhaseMap.Add(Handle, {IncomingTag})
       └─ 通知 PhaseStartObservers

PhaseAbility::EndAbility
  └─ Subsystem->OnEndPhase(this, Handle):
       ├─ PhaseEndedCallback(PhaseAbility)
       ├─ ActivePhaseMap.Remove(Handle)
       └─ 通知 PhaseEndObservers
```

---

## 五、关键函数

### UDark_TdoreGamePhaseSubsystem

| 函数 | 作用 | 调用者 |
|------|------|--------|
| `StartPhase(PhaseClass, Callback)` | 授予+激活阶段能力 | GameState::BeginPlay / 蓝图 |
| `OnBeginPhase(Ability, Handle)` | 取消冲突阶段 + 通知观察者 | PhaseAbility::ActivateAbility |
| `OnEndPhase(Ability, Handle)` | 执行回调 + 通知观察者 | PhaseAbility::EndAbility |
| `WhenPhaseStartsOrIsActive(Tag, ...)` | 注册阶段启动监听 | 任意系统 |
| `WhenPhaseEnds(Tag, ...)` | 注册阶段结束监听 | 任意系统 |
| `IsPhaseActive(Tag)` | 查询阶段是否活跃 | 蓝图/逻辑判断 |

---

## 六、互斥规则表

| 当前活跃 | 新启动 | 结果 | 原因 |
|----------|--------|------|------|
| PreGame | Playing | PreGame 被取消 | Playing 不匹配 PreGame |
| Playing | SuddenDeath | 两者共存 | SuddenDeath 匹配 Playing（父子） |
| WarmUp | Playing | WarmUp 被取消 | Playing 不匹配 WarmUp |
| Playing+SuddenDeath | PostGame | 全部被取消 | PostGame 不匹配 Playing |

---

## 七、文件清单

| 文件 | 作用 |
|------|------|
| `AbilitySystem/Phases/Dark_TdoreGamePhaseLog.h` | 日志类别声明 |
| `AbilitySystem/Phases/Dark_TdoreGamePhaseAbility.h/.cpp` | 阶段能力基类（GA） |
| `AbilitySystem/Phases/Dark_TdoreGamePhaseSubsystem.h/.cpp` | 阶段管理子系统（WorldSubsystem） |
| `GameModes/Dark_TdoreGameState.h/.cpp` | GameState：ASC 宿主 + BeginPlay 启动初始阶段 |
| `Config/DefaultGameplayTags.ini` | GamePhase.* 标签定义 |
