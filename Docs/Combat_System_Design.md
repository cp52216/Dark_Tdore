# Dark_Tdore 战斗系统设计参考

本文档是主角战斗系统第一阶段的项目参考。它吸收了你提供的 `deepseek_markdown_20260605_18566b.md` 里的动作游戏设计要点，并参考 Lyra 的 GAS、输入标签、装备授予技能这些架构方式，但不照搬 FPS 武器逻辑。

## 总方向

Dark_Tdore 的战斗系统要做到：数据驱动、武器驱动、动画窗口驱动。

玩家按下输入标签，Pawn 上的战斗输入缓冲组件记录指令；GAS Ability 负责权限、消耗、互斥、蒙太奇生命周期；动画通知打开预输入、取消、命中、位移窗口；武器和连招数据决定当前段能派生什么。Character 不应该知道具体连招顺序。

## 分层设计

1. 输入层
   Enhanced Input 仍然把按键映射到 `InputTag.*`。`UDark_TdoreAbilitySystemComponent` 继续作为 Lyra 风格输入路由。`UDark_TdoreCombatInputBufferComponent` 是可选 Pawn 组件，负责缓存最近输入。

2. 装备层
   武器通过 Equipment Definition / Ability Set 授予战斗 Ability。武器运行时状态放在 `UDark_TdoreWeaponInstance` 或它的子类里。可见武器使用 `ADark_TdoreWeaponActor` 或它的蓝图子类。

3. 技能层
   近战连招技能继承装备技能，而不是 Character 技能。技能从当前装备拿武器实例和连招数据，启动当前段动画，并且只在动画窗口允许时消费预输入。

4. 动画窗口层
   动画通知状态负责标记预输入窗口、取消窗口、命中窗口、位移/吸附窗口。设计师调动画资产上的时间点，C++ 负责稳定回调和校验。

5. 命中结算层
   下一阶段需要增加命中窗口通知和近战 Trace 组件。命中时去重同一挥砍内的目标，构造攻击数据，应用 GameplayEffect。攻击类型、冲击等级、格挡结果、削韧/硬直规则应进入数据，并最终扩展到 `FDark_TdoreGameplayEffectContext`。

## 核心规则

- 不把连招顺序写在 Character 里。
- 不把每把武器的动画时间写死在 C++ 里。
- 不依赖“蒙太奇通知名字字符串”作为唯一战斗真相。
- 预输入用 C++ 中间层处理，不用 Enhanced Input 注入作为正式方案。
- 服务器决定伤害和状态变化，本地预测以后再做表现优化。

## 当前第一阶段已经落地

- `UDark_TdoreCombatInputBufferComponent`
  缓存最近输入标签，暴露预输入窗口开关，并按允许标签消费输入。

- `FDark_TdoreComboStep` / `UDark_TdoreComboData`
  连招段数据模型：蒙太奇、Section、输入标签、下一段允许输入、攻击强度、冲击等级、伤害 GE、位移、吸附。

- `UAnimNotifyState_DarkTdoreInputBufferWindow`
  动画通知状态，进入通知时打开预输入窗口，离开通知时关闭窗口。

- `UDark_TdoreGameplayAbility_MeleeCombo`
  通用近战连招 Ability 基类。它要求来自装备授予，能拿到武器实例，能消费输入缓冲，并把“开始某一段连招”开放给蓝图实现。

## 来自参考文档的关键结论

- 轻攻击、重攻击、投技等分类要进入攻击数据，用于伤害、格挡、硬直和表现。
- 战斗位移应该曲线驱动，不要用简单匀速公式硬推。
- 预输入缓冲适合 C++ 中间层统一处理。
- 普通攻击先算命中/伤害再播表现；处决可以先预判能否击杀，再延迟结算最终伤害。
- 锁定视角、45 度相机和手动旋转需要明确区分角色朝向与控制器朝向。
- 极限闪避可以用残留碰撞代理检测，再延迟播放慢动作表现。
- 攻击吸附应数据化：最小/最大距离、吸附点、曲线、Motion Warping Target。

## 下一步路线

1. 创建 `IA_AttackLight`，绑定 `InputTag.Ability.Attack.Light`。
2. 从 `UDark_TdoreGameplayAbility_MeleeCombo` 创建 `GA_MeleeCombo_Sword` 蓝图。
3. 从 `UDark_TdoreComboData` 创建 `DA_Combo_Sword_Light`。
4. 把 `UDark_TdoreCombatInputBufferComponent` 挂到玩家 Pawn 蓝图。
5. 在剑攻击蒙太奇上添加 `DarkTdore Input Buffer Window` 动画通知状态。
6. 在 Ability 蓝图的 `Start Combo Step` 里播放当前段 Montage/Section。
7. 下一轮增加命中窗口通知、近战 Trace 组件、每段攻击去重、伤害 GE 应用。
