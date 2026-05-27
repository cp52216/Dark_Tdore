# 阵营系统 (Team System) — 完整指南

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

## 二、完整调用链路 — 从配置到友伤检测

### 阶段 1：配置层 — 数据结构初始化

```
GameMode 蓝图
  └─ GameState Class = Dark_TdoreGameState

Dark_TdoreGameState 构造:
  └─ TeamCreationComponent = CreateDefaultSubobject<UDark_TdoreTeamCreationComponent>()
       ├─ PublicTeamInfoClass = ADark_TdoreTeamPublicInfo::StaticClass()
       └─ TeamsToCreate = {}  ← 蓝图配置，如 {0→DA_TeamRed, 1→DA_TeamBlue}
```

```
PlayerState (每个玩家)
  └─ MyTeamID = FGenericTeamId::NoTeam             ← 初始无阵营
      OnTeamChangedDelegate                        ← 阵营变更通知
```

### 阶段 2：初始化层 — 阵营 Actor 创建与注册

```
World 初始化
  └─ UDark_TdoreTeamSubsystem 自动创建 (UWorldSubsystem)

GameState::BeginPlay
  └─ TeamCreationComponent::BeginPlay
       │
       ├─ for (TeamsToCreate) → CreateTeam(TeamId, DisplayAsset)
       │    │
       │    ├─ World::SpawnActor<ADark_TdoreTeamPublicInfo>()   ← 生成阵营Actor
       │    ├─ NewPublicInfo->SetTeamId(0)                        ← 设置ID
       │    │    └─ TeamId = 0
       │    │    └─ TryRegisterWithTeamSubsystem()
       │    │         └─ TeamSubsystem::RegisterTeamInfo(this)
       │    │              └─ TeamMap.FindOrAdd(0).SetTeamInfo(...)
       │    │                   ├─ PublicInfo = NewPublicInfo
       │    │                   └─ DisplayAsset = DA_TeamRed (if set)
       │    │
       │    └─ NewPublicInfo->SetTeamDisplayAsset(DA_TeamRed)
       │         └─ 重新注册一次，刷新 DisplayAsset 缓存
       │
       └─ for (PlayerArray) → 分配玩家到人数最少的阵营
            └─ DarkPS->SetGenericTeamId(IntegerToGenericTeamId(BestTeamId))
```

### 阶段 3：归属层 — 玩家加入阵营

```
Dark_TdorePlayerState::SetGenericTeamId(TeamID=0)
  │
  ├─ OldTeamID = MyTeamID     ← 保存旧值 (NoTeam = -1)
  ├─ MyTeamID = NewTeamID     ← 写入新值 (Team 0)
  │
  └─ ConditionalBroadcastTeamChanged(this, OldTeamID, NewTeamID)
       │
       ├─ OldTeamID ≠ NewTeamID  → 确实变化了
       │
       ├─ GetTeamChangedDelegateChecked().Broadcast(this, -1, 0)
       │    └─ 通知所有监听者：该玩家从无阵营变为 Team 0
       │         (UI、GameplayCue、其他系统可监听此委托)
       │
       └─ UE_LOG: "[TeamAgent] BP_DarkTdorePlayerState_C_0 阵营变更: -1 → 0"
```

此时子系统查询链：
```
TeamSubsystem::FindTeamFromObject(BP_ThirdPersonCharacter)
  → Cast<IDark_TdoreTeamAgentInterface>(Character) → Character没实现
  → Cast<const APawn>(Character)
    → Pawn->GetPlayerState<ADark_TdorePlayerState>()
      → Cast<IDark_TdoreTeamAgentInterface>(PS)
        → PS->GetGenericTeamId()  → TeamId = 0  ✅
```

### 阶段 4：战斗层 — 友伤检测

```
Q键 → BP_GA_TestQ 激活 → ApplyGameplayEffectToOwner(GE_Damage_20)
  └─ DamageExecution::Execute_Implementation
       │
       ├─ HitActor = BP_ThirdPersonCharacter_C_0  ← 受击目标
       ├─ EffectCauser = BP_ThirdPersonCharacter_C_0  ← 攻击者 (同一人)
       │
       └─ TeamSubsystem->CanCauseDamage(EffectCauser, HitActor)
            │
            ├─ Step 1: 查找双方阵营
            │    FindTeamFromObject(EffectCauser) → 通过PS查到 TeamId = 0
            │    FindTeamFromObject(HitActor)     → 通过PS查到 TeamId = 0
            │
            ├─ Step 2: 对比阵营
            │    CompareTeams → TeamIdA==TeamIdB → OnSameTeam
            │
            ├─ Step 3: 日志打印
            │    "[TeamSubsystem] CanCauseDamage:
            │      Instigator=BP_ThirdPersonCharacter(Team=0) 
            │      Target=BP_ThirdPersonCharacter(Team=0) 
            │      → 同阵营-禁止 | 自伤=1"
            │
            └─ Step 4: 判定规则
                 ├─ 规则1: 自伤? bAllowDamageToSelf=true && 同一人 → return true ✅
                 ├─ 规则2: 不同阵营? No → 继续
                 ├─ 规则3: 无阵营+有ASC? No → 继续
                 └─ 规则4: 同阵营 → return false ← 友伤阻止！
                        但自伤已在前方return，所以最终返回 true
```

### 阶段 5：不同对象的场景（非自伤）

```
如果玩家A (Team0) 攻击 玩家B (Team0):
  
  CanCauseDamage(A, B):
    → 自伤? A≠B → false, 不短路
    → FindTeam(A)=0, FindTeam(B)=0 → OnSameTeam
    → 规则2: DifferentTeams? No
    → 规则3: 跳过
    → 规则4: return false ❌  — 友伤阻止！

如果玩家A (Team0) 攻击 玩家B (Team1):

  CanCauseDamage(A, B):
    → 自伤? A≠B → false, 不短路  
    → FindTeam(A)=0, FindTeam(B)=1 → DifferentTeams
    → 日志: "不同阵营-允许"
    → 规则2: DifferentTeams → return true ✅  — 允许伤害！
```

---

## 三、CanCauseDamage 判定规则

```
┌──────────────────────────────────────────────────────────┐
│ CanCauseDamage(Instigator, Target, bAllowDamageToSelf)    │
│                                                            │
│ 1. 先对比阵营（打印日志用于诊断）                          │
│    CompareTeams → OnSameTeam / DifferentTeams / Invalid    │
│                                                            │
│ 2. 规则1: bAllowDamageToSelf=true && Instigator==Target   │
│    → return true   (自伤允许)                              │
│                                                            │
│ 3. 规则2: DifferentTeams                                  │
│    → return true   (敌人伤害允许)                          │
│                                                            │
│ 4. 规则3: InvalidArgument && InstigatorTeamId != INDEX_NONE│
│    && Target有ASC                                         │
│    → return true   (训练假人/环境伤害允许)                 │
│                                                            │
│ 5. 规则4: 同阵营                                           │
│    → return false  (友伤阻止)                              │
└──────────────────────────────────────────────────────────┘
```

---

## 四、核心数据结构

### TeamSubsystem::TeamMap

```
TMap<int32, FDark_TdoreTeamTrackingInfo>

TeamMap[0] = {
    PublicInfo:  ADark_TdoreTeamPublicInfo(TeamId=0, DisplayAsset=DA_TeamRed)
    PrivateInfo: null (暂未实现)
    DisplayAsset: DA_TeamRed
    OnTeamDisplayAssetChanged: 显示资产变更委托
}

TeamMap[1] = {
    PublicInfo:  ADark_TdoreTeamPublicInfo(TeamId=1, DisplayAsset=DA_TeamBlue)
    ...
}
```

### FindTeamFromObject 查找优先级

```
1. 自身实现 IDark_TdoreTeamAgentInterface → 直接查 GetGenericTeamId()
2. Actor 的 Instigator 实现了接口        → 查 Instigator
3. 自身是 TeamInfo Actor                  → 直接读 TeamId
4. Pawn/Controller → PlayerState          → 查 PS 的 TeamId
5. 全都不满足                             → return INDEX_NONE
```

### GameplayTagStack（阵营标签堆叠）

```
TeamTags (FGameplayTagStackContainer):
  存储在 ADark_TdoreTeamInfoBase 上，通过网络复制

  用法：
    TeamTags.AddStack("Status.HasFlag", 1);       // 阵营持有旗帜
    TeamTags.AddStack("Score.Multiplier", 2);     // 阵营分数 ×2
    int32 Count = TeamTags.GetStackCount(Tag);    // 查询层数
    bool bHas = TeamTags.ContainsTag(Tag);        // 是否有标签
```

---

## 五、文件清单

| 文件 | 作用 |
|------|------|
| `Teams/Dark_TdoreTeamAgentInterface.h/.cpp` | 阵营代理接口（继承 UGenericTeamAgentInterface） |
| `Teams/Dark_TdoreTeamSubsystem.h/.cpp` | 核心：UWorldSubsystem，阵营管理+友伤判断 |
| `Teams/Dark_TdoreTeamDisplayAsset.h/.cpp` | 阵营视觉资产（颜色/纹理/名称 DataAsset） |
| `Teams/Dark_TdoreTeamInfoBase.h/.cpp` | 阵营 Actor 基类（含 FGameplayTagStackContainer） |
| `Teams/Dark_TdoreTeamPublicInfo.h/.cpp` | 阵营公开信息 Actor（含显示资产） |
| `Teams/Dark_TdoreTeamCreationComponent.h/.cpp` | GameStateComponent，创建阵营+分配玩家 |
| `Teams/Dark_TdoreTeamStatics.h/.cpp` | 蓝图工具函数 |
| `System/GameplayTagStack.h/.cpp` | GameplayTag 堆叠系统（FFastArraySerializer 网络复制） |
| `GameModes/Dark_TdoreGameState.h/.cpp` | GameState，包含 TeamCreationComponent |
| `Player/Dark_TdorePlayerState.h/.cpp` | 实现 IDark_TdoreTeamAgentInterface |
| `AbilitySystem/Executions/Dark_TdoreDamageExecution.cpp` | 友伤检测调用点 |

---

## 六、测试验证

### 快速测试（当前可用）

1. **BP_ThirdPersonGameMode** → Class Defaults → GameState Class → `Dark_TdoreGameState`
2. 按 Q → 观察日志：
   ```
   [TeamAgent] BP_DarkTdorePlayerState_C_0 阵营变更: -1 → 0    ← 阵营分配成功
   [TeamSubsystem] CanCauseDamage: ... Team=0 → 同阵营-禁止 | 自伤=1  ← 友伤检测生效
   ```

### 完整测试（需两个角色）

1. 创建 `BP_GameState` 子类 → 配 `TeamsToCreate: {0, 1}`
2. 两个同阵营角色互相攻击 → 伤害归零
3. 两个不同阵营角色互相攻击 → 正常伤害
