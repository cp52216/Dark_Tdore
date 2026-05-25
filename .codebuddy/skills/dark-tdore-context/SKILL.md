# Dark_Tdore 项目上下文 Skill

> **用途**: 跨对话保持项目架构记忆，防止上下文丢失。
> **自动更新**: 该 Skill 绑定 Git 提交自动同步机制。
> **最后更新**: 2026-05-25

---

## 一、项目元信息

| 属性 | 值 |
|------|-----|
| 项目名 | Dark_Tdore |
| 引擎版本 | UE 5.7 |
| 架构风格 | Lyra Starter Game 参考 |
| 主要模块 | `Dark_Tdore` (Runtime) |
| 附加依赖 | Engine, UMG |
| 内置插件 | GameplayAbilities, ModularGameplay, ModularGameplayActors, EnhancedInput, ModelingToolsEditorMode |
| 项目插件 | Monolith (v0.14.10, AI-MCP), ModularGameplayActors |

## 二、目录结构

```
Dark_Tdore/
├── Source/Dark_Tdore/           # 核心运行时模块
│   ├── AbilitySystem/           # GAS 系统
│   │   ├── Abilities/           # 具体技能 (GA_Death, GA_TestQ)
│   │   ├── Attributes/          # 属性集 (HealthSet, CombatSet, AttributeSet基类)
│   │   ├── Dark_TdoreAbilitySet         # DataAsset: 技能+GE 捆绑
│   │   ├── Dark_TdoreAbilitySystemComponent  # 自定义 ASC
│   │   ├── Dark_TdoreGameplayAbility     # GA 基类
│   │   └── Dark_TdoreLogChannels        # 日志通道
│   ├── Camera/                  # 摄像机系统 (模仿 Lyra)
│   │   ├── Dark_TdoreCameraComponent           # 摄像机组件
│   │   ├── Dark_TdoreCameraMode               # 模式基类+视图容器+混合栈
│   │   ├── Dark_TdoreCameraMode_ThirdPerson    # 第三人称实现
│   │   └── Dark_TdorePenetrationAvoidanceFeeler # 防穿透射线
│   ├── Character/               # 角色/Pawn 系统
│   │   ├── Dark_TdoreCharacterMovementComponent  # 自定义移动
│   │   ├── Dark_TdoreHealthComponent              # 血量管理(死亡流程)
│   │   ├── Dark_TdoreHeroComponent                # 玩家输入(Lyra风格)
│   │   ├── Dark_TdorePawn                         # AI/NPC 基类
│   │   ├── Dark_TdorePawnData                     # DataAsset: 角色配置
│   │   └── Dark_TdorePawnExtensionComponent       # InitState协调器
│   ├── Input/                   # 输入系统
│   │   └── Dark_TdoreInputConfig                 # 输入配置 DataAsset
│   └── Player/                  # 玩家状态
│       └── Dark_TdorePlayerState                 # ASC 宿主(Lyra风格)
├── Plugins/
│   ├── Monolith/                # UE MCP 插件 (1290+ 操作)
│   └── ModularGameplayActors/   # 模块化角色框架
├── Docs/
│   ├── GAS_System_Guide.md      # GAS 完整加载触发流程
│   └── Camera_System_Guide.md   # 摄像机系统详解
├── Config/
│   ├── DefaultEngine.ini, DefaultGame.ini
│   ├── DefaultInput.ini, DefaultGameplayTags.ini
│   └── DefaultEditor.ini, DefaultEditorPerProjectUserSettings.ini
└── .codebuddy/skills/           # CodeBuddy Skills
    ├── lyra-references/         # Lyra 架构参考
    ├── monolith-ue-mcp/         # Monolith MCP 操作指南
    └── dark-tdore-context/      # 本 Skill
```

## Content 目录结构

> 自动生成，每次 commit 时刷新

| 目录 | 文件数 | 类型 | 子目录 |
|------|--------|------|--------|
| Abilities | - | 蓝图资产 | - |
| Blueprints | - | 蓝图资产 | - |
| Characters | 128 | .uasset | - |
| Collections | - | 收藏集 | - |
| Developers | - | 开发者内容 | - |
| Input | 11 | .uasset | - |
| LevelPrototyping | 29 | .uasset | - |
| ShuangDao | 186 | .uasset(185), .umap(1) | - |
| System | - | 系统配置资产 | - |
| ThirdPerson | - | 第三人称模板 | - |
| Variant_Combat | 31 | .uasset(30), .umap(1) | - |
| Variant_Platforming | 11 | .uasset(10), .umap(1) | - |
| Variant_SideScrolling | 19 | .uasset(18), .umap(1) | - |
| WuDang_Montage | 371 | .uasset(370), .umap(1) | - |
| __ExternalActors__ | 556 | .uasset | (引擎管理) |
| __ExternalObjects__ | 42 | .uasset | (引擎管理) |

## 三、核心类继承关系

```
UObject
├── AActor
│   ├── APawn
│   │   ├── ADark_TdorePawn (AI/NPC基类)
│   │   └── ACharacter → AModularCharacter → ADark_TdoreCharacter (玩家)
│   ├── APlayerState → AModularPlayerState → ADark_TdorePlayerState (ASC宿主)
│   ├── APlayerController → ADark_TdorePlayerController (PostProcessInput)
│   └── AGameModeBase → ADark_TdoreGameMode
├── UActorComponent
│   ├── UPawnComponent
│   │   ├── UDark_TdorePawnExtensionComponent (InitState协调器)
│   │   ├── UDark_TdoreHeroComponent (输入处理+摄像机委托)
│   │   └── UDark_TdoreCameraComponent (摄像机模式栈)
│   └── UGameFrameworkComponent
│       └── UDark_TdoreHealthComponent (血量+死亡流程)
├── UGameplayAbility → UDark_TdoreGameplayAbility (GA基类)
├── UAbilitySystemComponent → UDark_TdoreAbilitySystemComponent (ASC)
├── UAttributeSet → UDark_TdoreAttributeSet (属性基类)
│   ├── UDark_TdoreHealthSet (生命值管线)
│   └── UDark_TdoreCombatSet (战斗数值)
└── UPrimaryDataAsset
    ├── UDark_TdorePawnData (角色配置捆绑)
    └── UDark_TdoreAbilitySet (技能集合)
```

## 四、GAS 系统架构 (参考 Lyra)

### ASC 在 PlayerState 上

| 元素 | 位置 | 说明 |
|------|------|------|
| ASC 创建 | `ADark_TdorePlayerState::constructor` | `CreateDefaultSubobject<UDark_TdoreAbilitySystemComponent>` |
| ASC 初始化 | `PostInitializeComponents` | `ASC->InitAbilityActorInfo(this, GetPawn())` |
| Avatar 修正 | `Character::PossessedBy` | `PawnExtComp->InitializeAbilitySystem(ASC, PS)` |
| 技能授予 | `PlayerState::BeginPlay` | `AbilitySet->GiveToAbilitySystem(ASC, SourceObject)` |
| 标签挂载 | `AbilitySet::GiveToAbilitySystem` | `Spec.GetDynamicSpecSourceTags().AddTag(InputTag)` |
| 标签匹配 | ASC 内 | `Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag)` |
| 输入路由 | `PlayerController::PostProcessInput` | `ASC->ProcessAbilityInput(DeltaTime, bGamePaused)` |

### 自定义 ASC (UDark_TdoreAbilitySystemComponent)

- **输入路由**: `AbilityInputTagPressed(Tag)` / `AbilityInputTagReleased(Tag)` / `ProcessAbilityInput()` / `ClearAbilityInput()`
- **激活组管理**: `IsActivationGroupBlocked()` / `AddAbilityToActivationGroup()` / `RemoveAbilityFromActivationGroup()` / `CancelActivationGroupAbilities()`
- **OnSpawn 技能**: `TryActivateAbilitiesOnSpawn()`
- **动态标签效果**: `AddDynamicTagGameplayEffect(Tag)` / `RemoveDynamicTagGameplayEffect(Tag)`
- **内部缓冲**: `InputPressedSpecHandles` / `InputReleasedSpecHandles` / `InputHeldSpecHandles` / `ActivationGroupCounts[4]`

### GA 基类 (UDark_TdoreGameplayAbility)

- **ActivationPolicy**: `OnInputTriggered` / `WhileInputActive` / `OnSpawn`
- **ActivationGroup**: `Independent` / `Exclusive_Replaceable` / `Exclusive_Blocking`
- **蓝图事件**: `K2_OnAbilityAdded()` / `K2_OnAbilityRemoved()` / `K2_OnPawnAvatarSet()`
- 覆写 `CanActivateAbility` 检查阻塞组

### 属性集

| 类 | 属性 | 职责 |
|----|------|------|
| `UDark_TdoreAttributeSet` | (基类) | `ATTRIBUTE_ACCESSORS` 宏、`FDarkTdoreAttributeEvent` 委托类型 |
| `UDark_TdoreHealthSet` | Health, MaxHealth, Healing, Damage | Meta属性管道: Damage→Health-、Healing→Health+、Clamp[0,MaxHealth] |
| `UDark_TdoreCombatSet` | (战斗属性) | 战斗相关数值 |

### 现有技能

| 技能 | 类 | 功能 |
|------|-----|------|
| `GA_TestQ` | `UDark_TdoreGameplayAbility` | Q 键测试技能 |
| `GA_Death` | `UDark_TdoreGameplayAbility` | 死亡处理技能 (`GameplayEvent.Death` 触发) |

### 新增技能流程 (零 C++)

1. `DefaultGameplayTags.ini` → 添加 `Input.Ability.X` 标签
2. 创建 GA 蓝图 (父类: `Dark_TdoreGameplayAbility`)
3. `DA_DefaultAbilitySet` → `GrantedAbilities` 加行
4. `DA_InputConfig` → `AbilityInputActions` 加行
5. `IMC_Default` → 按键映射

### GameplayTags 清单

```
输入:  InputTag.Move/Look/LookMouse/Jump, Input.Ability.Q
初始化: InitState.Spawned/DataAvailable/DataInitialized/GameplayReady
状态:  Status.Death, Status.Death.Dying/Dead
事件:  GameplayEvent.Death
行为:  Ability.Behavior.SurvivesDeath, Ability.Type.Action
效果:  GameplayEffect.Damage/Heal, GameplayCue.Character.DamageTaken/Heal
免疫:  Gameplay.DamageImmunity, Gameplay.DamageSelfDestruct
移动:  Gameplay.MovementStopped
```

## 五、摄像机系统 (模仿 Lyra)

### 核心组件

| 类 | 职责 |
|----|------|
| `UDark_TdoreCameraComponent` | 摄像机组件，管理模式栈 (`CameraModeStack`)，每帧 `GetCameraView()` |
| `UDark_TdoreCameraMode` | 模式基类：视图容器 `FCameraModeView` + 混合栈逻辑 |
| `UDark_TdoreCameraMode_ThirdPerson` | 第三人称实现：偏移/蹲伏/穿透避免(7条扇形射线) |
| `UDark_TdorePenetrationAvoidanceFeeler` | 防穿透射线数据结构 |

### 数据流

1. `HeroComponent::DetermineCameraMode()` 委托 → 返回当前模式
2. 模式优先级: `AbilityCameraMode`(技能覆盖) > `PawnData->DefaultCameraMode`(默认)
3. `CameraComponent::PushCameraMode()` → `CameraModeStack` 推入 → 混合过渡
4. 每帧 `EvaluateStack()` → `UpdateView()` → `BlendStack()`
5. GA 技能通过 `HeroComponent::SetAbilityCameraMode()` 临时覆盖

### 蓝图配置

| 蓝图 | 路径 | 继承 |
|------|------|------|
| `CM_ThirdPerson` | `/Game/Camera/CM_ThirdPerson` | `UDark_TdoreCameraMode_ThirdPerson` |
| `DA_PawnData` | `/Game/System/DA_PawnData` | `UDark_TdorePawnData` |

## 六、初始化流程

### InitState 状态机 (GameFrameworkInitStateInterface)

```
Spawned → DataAvailable → DataInitialized → GameplayReady
```

- `UDark_TdorePawnExtensionComponent`: 协调者，管理 ASC 关联
- `UDark_TdoreHeroComponent`: 在 `DataInitialized` 时绑定输入+摄像机委托

### 完整启动顺序

1. PlayerState 创建 → ASC/AttributeSet 创建 → `InitAbilityActorInfo(PS, nullptr)`
2. Character 创建 → 组件构造 (PawnExtComp, HeroComp, CameraComp, HealthComp)
3. `PossessedBy()` → `PawnExtComp->InitializeAbilitySystem()` → Avatar 修正
4. `SetupPlayerInputComponent` → `HeroComponent->InitializePlayerInput()` → EnhancedInput 绑定
5. `HeroComponent->DetermineCameraMode` 委托绑定 → 首帧推入默认摄像机

## 七、输入系统

| 组件 | 职责 |
|------|------|
| `UDark_TdoreInputConfig` | DataAsset: `NativeInputActions` + `AbilityInputActions` |
| `UDark_TdoreHeroComponent` | 输入绑定+处理 (`Input_Move/Look/Jump`) |
| `ADark_TdorePlayerController` | `PostProcessInput` → `ASC->ProcessAbilityInput()` |

## 八、血量/死亡系统

- **`UDark_TdoreHealthComponent`**: 监听 `HealthSet` 属性变化
- 死亡状态机: `NotDead → DeathStarted → DeathFinished`
- 标签: `Status.Death.Dying` / `Status.Death.Dead`
- 事件: `OnDeathStarted` / `OnDeathFinished` 委托 → `GA_Death` 技能
- `Ability.Behavior.SurvivesDeath` 标签: 带有此标签的技能不会被死亡取消

## 九、插件详情

### Monolith (v0.14.10)
- 1290+ 操作，16 个命名空间
- 支持: Blueprint(89), Material(63), Animation(120), Niagara(109), Mesh(194), Editor(29), GAS(135), AI(221), Audio(98), UI(121), LevelSequence(8) 等
- 依赖: Niagara, SQLiteCore, EnhancedInput, PythonScriptPlugin, CommonUI, StateTree 等

### ModularGameplayActors
- 提供 `AModularCharacter`, `AModularPlayerState` 等模块化基类

## 十、构建配置

- **Build.cs 公共依赖**: Core, CoreUObject, Engine, InputCore, EnhancedInput, GameplayAbilities, GameplayTags, GameplayTasks, ModularGameplay, ModularGameplayActors, UMG, Slate
- **公开头文件路径**: `Dark_Tdore`, `Dark_Tdore/AbilitySystem`, `Dark_Tdore/AbilitySystem/Abilities`

## 十一、编码规范

- 类前缀: `Dark_Tdore` (如 `UDark_TdoreGameplayAbility`)
- ASC 在 PlayerState（不在 Character），模仿 Lyra
- HeroComponent 处理所有输入（不在 Character），模仿 Lyra
- 摄像机使用模式栈 + 委托（不在 Character 上直接操作 SpringArm）
- PostProcessInput 处理技能输入队列
- 使用 InitState 状态机协调组件初始化
- DataAsset 驱动: PawnData, AbilitySet, InputConfig

---

## 自动更新机制

本 Skill 通过 **Git Hook + 增量检测 + 自动提交** 三个机制保持同步：

### 工作流程
```
git commit (任何代码变更, 含 revert 回退)
    │
    └─ .git/hooks/post-commit 触发
          ├─ 防递归检查 ([skill-auto] 标记则跳过)
          ├─ 运行 update-skill.py (git diff 增量检测)
          └─ 若 SKILL.md 有变更 → git add + git commit [skill-auto]
```

### 触发时机
| 方式 | 触发条件 | 说明 |
|------|----------|------|
| Git Hook | 每次 `git commit` | 增量检测，跟踪所有 changed files |
| 回退代码 | `git revert` 产生新 commit | 自然触发 Hook |
| 手动触发 | 告诉 AI "更新项目上下文" | 全量/增量可选 |

### 核心脚本
- `.codebuddy/skills/dark-tdore-context/update-skill.py` — 基于 `git diff HEAD~1 HEAD` 增量检测
- `.git/hooks/post-commit` (bash) — 自动运行 + 自动提交
- `.git/hooks/post-commit.ps1` (PowerShell) — Windows 原生版

### 防递归
自动提交信息包含 `[skill-auto]` 标记，Hook 检测到此标记自动跳过，防止无限循环。
