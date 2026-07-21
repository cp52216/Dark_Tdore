# 阵营系统 (Team System) — 完整指南

## Current Project Notes

1. 实际伤害判定入口是 `Dark_TdoreDamageExecution.cpp` 中对 `TeamSubsystem->CanCauseDamage(...)` 的调用。
2. 玩家能力来源已经统一走 `PawnData->AbilitySets`。

> 参考 Lyra Teams 目录实现，核心基于 `UWorldSubsystem` + `IGenericTeamAgentInterface`

---

## 一、架构概览

```
Dark_TdoreGameState
  └─ TeamCreationComponent (UGameStateComponent)
       ├─ TeamsToCreate: {0→DA_TeamRed, 1→DA_TeamBlue}
       └─ BeginPlay → 创建阵营Actor + 分配玩家

World
  └─ TeamSubsystem (UWorldSubsystem, 自动创建)
       ├─ TeamMap: TMap<int32, FTeamTrackingInfo>  ← 核心数据结构
       ├─ RegisterTeamInfo / UnregisterTeamInfo
       ├─ FindTeamFromObject
       ├─ CompareTeams
       └─ CanCauseDamage

PlayerState
  └─ 实现 IDark_TdoreTeamAgentInterface
       ├─ MyTeamID (FGenericTeamId, 复制到客户端)
       └─ OnTeamChangedDelegate

DamageExecution
  └─ CanCauseDamage(Instigator, Target) → 友伤检测
```

---

## 二、完整调用链路

### 阶段 1：配置层

```
GameMode 蓝图 → GameState Class = Dark_TdoreGameState
Dark_TdoreGameState 构造:
  └─ TeamCreationComponent = CreateDefaultSubobject
       └─ TeamsToCreate = {}  ← 蓝图配置
```

### 阶段 2：阵营 Actor 创建与注册

```
GameState::BeginPlay
  └─ TeamCreationComponent::BeginPlay
       ├─ for (TeamsToCreate) → CreateTeam(TeamId, DisplayAsset)
       │    ├─ SpawnActor<ADark_TdoreTeamPublicInfo>()
       │    └─ TryRegisterWithTeamSubsystem()
       └─ for (PlayerArray) → 分配玩家到人数最少的阵营
```

### 阶段 3：玩家加入阵营

```
Dark_TdorePlayerState::SetGenericTeamId(TeamID=0)
  ├─ MyTeamID = NewTeamID
  └─ ConditionalBroadcastTeamChanged → OnTeamChangedDelegate.Broadcast
```

### 阶段 4：FindTeamFromObject 查找优先级

```
1. 自身实现 IDark_TdoreTeamAgentInterface → GetGenericTeamId()
2. Actor 的 Instigator 实现了接口
3. 自身是 TeamInfo Actor → 直接读 TeamId
4. Pawn/Controller → PlayerState → PS 的 TeamId
5. 全都不满足 → return INDEX_NONE
```

### 阶段 5：友伤检测

```
DamageExecution::Execute_Implementation
  └─ TeamSubsystem->CanCauseDamage(EffectCauser, HitActor)
       ├─ FindTeamFromObject(Instigator) + FindTeamFromObject(Target)
       ├─ CompareTeams → OnSameTeam / DifferentTeams / Invalid
       └─ 判定规则:
            ├─ 规则1: bAllowDamageToSelf && Instigator==Target → true (自伤)
            ├─ 规则2: DifferentTeams → true (敌人伤害)
            ├─ 规则3: Invalid && Instigator有ASC && Target有ASC → true (训练假人)
            └─ 规则4: 同阵营 → false (友伤阻止)
```

---

## 三、文件清单

| 文件 | 作用 |
|------|------|
| `Teams/Dark_TdoreTeamAgentInterface.h/.cpp` | 阵营代理接口 |
| `Teams/Dark_TdoreTeamSubsystem.h/.cpp` | 核心：UWorldSubsystem，阵营管理+友伤判断 |
| `Teams/Dark_TdoreTeamDisplayAsset.h/.cpp` | 阵营视觉资产（颜色/纹理/名称 DataAsset） |
| `Teams/Dark_TdoreTeamInfoBase.h/.cpp` | 阵营 Actor 基类 |
| `Teams/Dark_TdoreTeamPublicInfo.h/.cpp` | 阵营公开信息 Actor |
| `Teams/Dark_TdoreTeamCreationComponent.h/.cpp` | GameStateComponent，创建阵营+分配玩家 |
| `Teams/Dark_TdoreTeamStatics.h/.cpp` | 蓝图工具函数 |
| `System/GameplayTagStack.h/.cpp` | GameplayTag 堆叠系统 |
| `GameModes/Dark_TdoreGameState.h/.cpp` | GameState，包含 TeamCreationComponent |
| `Player/Dark_TdorePlayerState.h/.cpp` | 实现 IDark_TdoreTeamAgentInterface |
| `AbilitySystem/Executions/Dark_TdoreDamageExecution.cpp` | 友伤检测调用点 |
