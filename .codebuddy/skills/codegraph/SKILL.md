# CodeGraph 项目级使用指南

## 概述

CodeGraph 是一个代码索引工具，已集成到 Dark_Tdore 项目中，用于快速查询符号、调用关系、影响范围等。

**索引状态**: 249 个文件，3,494 个节点，7,976 条边  
**索引位置**: `d:/UE_ProJect/Dark_Tdore/.codegraph/`

## 快速开始

所有命令都需要在项目根目录下执行：

```powershell
cd d:/UE_ProJect/Dark_Tdore
```

由于 PowerShell 执行策略限制，所有命令需要加 `powershell -ExecutionPolicy Bypass -Command "..."`。

## 核心命令

### 1. 搜索符号

查找类、函数、变量的定义和引用位置：

```powershell
powershell -ExecutionPolicy Bypass -Command "codegraph query 符号名 --limit 10"
```

示例：
```powershell
codegraph query Dark_TdoreAnimInstance --limit 5
codegraph query GiveToAbilitySystem --limit 10
codegraph query RefreshWeaponState --limit 10
```

### 2. 查询调用方（谁调用了这个函数）

```powershell
powershell -ExecutionPolicy Bypass -Command "codegraph callers 函数名"
```

示例：
```powershell
codegraph callers RefreshWeaponState
codegraph callers GiveToAbilitySystem
codegraph callers ActivateAbility
```

### 3. 查询被调用方（这个函数调用了谁）

```powershell
powershell -ExecutionPolicy Bypass -Command "codegraph callees 函数名"
```

示例：
```powershell
codegraph callees NativeUpdateAnimation
codegraph callees OnEquipped
codegraph callees StartComboStep
```

### 4. 影响范围分析（改代码前必做）

查询修改某个函数会影响哪些代码：

```powershell
powershell -ExecutionPolicy Bypass -Command "codegraph impact 函数名"
```

示例：
```powershell
codegraph impact RefreshWeaponState
codegraph impact GiveToAbilitySystem
codegraph impact SetWeaponEquippedTag
```

### 5. 索引管理

| 操作 | 命令 |
|------|------|
| 全量重建索引 | `codegraph index` |
| 强制全量重建 | `codegraph index --force` |
| 增量更新索引 | `codegraph index --incremental` |
| 实时监听文件变化 | `codegraph watch` |

## 工作流程建议

### 修改代码前

1. **先搜索符号**：确认目标代码位置
   ```powershell
   codegraph query 函数名 --limit 5
   ```

2. **再查调用关系**：了解上下游
   ```powershell
   codegraph callers 函数名  # 谁调用了它
   codegraph callees 函数名  # 它调用了谁
   ```

3. **最后查影响范围**：评估改动影响
   ```powershell
   codegraph impact 函数名
   ```

4. **兜底方案**：如果图谱结果不全，再用 `grep` / 手动读文件补充

### 修改代码后

更新索引保持同步：

```powershell
codegraph index --incremental
```

## 项目特定示例

### 动画系统

```powershell
# 查找动画实例相关代码
codegraph query Dark_TdoreAnimInstance --limit 10

# 查看谁调用了 RefreshWeaponState
codegraph callers RefreshWeaponState

# 查看 NativeUpdateAnimation 调用了哪些函数
codegraph callees NativeUpdateAnimation
```

### 装备系统

```powershell
# 查找装备管理器相关代码
codegraph query Dark_TdoreEquipmentManagerComponent --limit 10

# 查看谁调用了 AddEntry
codegraph callers AddEntry

# 查看 GiveToAbilitySystem 的影响范围
codegraph impact GiveToAbilitySystem
```

### 武器系统

```powershell
# 查找武器实例相关代码
codegraph query Dark_TdoreWeaponInstance --limit 10

# 查看谁调用了 OnEquipped
codegraph callers OnEquipped

# 查看 SetWeaponEquippedTag 调用了哪些函数
codegraph callees SetWeaponEquippedTag
```

### GAS 系统

```powershell
# 查找技能系统相关代码
codegraph query Dark_TdoreAbilitySystemComponent --limit 10

# 查看谁调用了 TryActivateAbility
codegraph callers TryActivateAbility

# 查看 ActivateAbility 的影响范围
codegraph impact ActivateAbility
```

## 注意事项

1. **索引范围**: CodeGraph 只索引 `.h/.cpp` 源码，不索引 `.uasset` 等二进制文件
2. **UE 临时目录**: `DerivedDataCache/`、`Intermediate/`、`Binaries/` 已在 `.gitignore` 中，不会被索引
3. **索引更新**: 大改代码后记得运行 `codegraph index --incremental` 更新索引
4. **查询限制**: 使用 `--limit` 参数限制返回结果数量，避免输出过多

## MCP 集成（可选）

已配置 `d:/UE_ProJect/Dark_Tdore/mcp.json`，重启 AI 工具后可直接调用 CodeGraph：

```json
{
  "mcpServers": {
    "codegraph": {
      "type": "stdio",
      "command": "codegraph",
      "args": ["serve", "--mcp"]
    }
  }
}
```

## 故障排查

### 查询结果为空

1. 确认索引是最新的：`codegraph index --incremental`
2. 确认符号名拼写正确
3. 尝试更宽泛的搜索：`codegraph query 部分符号名 --limit 20`

### 索引过期

```powershell
codegraph index --force
```

### 卸载 CodeGraph

```powershell
codegraph uninstall
```
