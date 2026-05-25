---
name: monolith-ue-mcp
description: >
  Monolith MCP plugin for Unreal Engine 5.7 — provides AI with full read/write access to UE projects
  through 16+ namespaces and 1290+ actions. This skill should be used when the user asks to create,
  modify, inspect, or search any Unreal Engine asset (Blueprints, Materials, Animation, Niagara,
  Mesh, AI, GAS, UI, Audio, Level Sequences, project search, C++ source lookup, or editor operations).
  Also use when the project has the Monolith plugin installed and the UE Editor is running on
  http://localhost:9316/mcp.
---

# Monolith UE MCP Skill

## Overview

Monolith is an in-process Unreal Engine editor MCP plugin exposing 1290+ operations across 16 namespaces
through 20 MCP tools. This skill governs how to effectively discover and invoke those tools.

## Prerequisites

- Monolith MCP is connected (check with `monolith_status`)
- UE Editor is running and the MCP server is listening on port 9316
- If tools return errors, check `monolith_status` first

## Tool Architecture

Each domain has a `*_query` tool that accepts an `action` string (enum) and a `params` object.
Always use `monolith_discover` to get the full parameter schema for an action before calling it.

### Core Discovery Tools

| Tool | Purpose |
|------|---------|
| `monolith_status` | Check server health, version, uptime, action count |
| `monolith_discover` | List available namespaces/categories or get action parameter schemas |
| `monolith_reindex` | Rebuild the project asset index |
| `monolith_update` | Check for Monolith plugin updates |

### Domain Tools (16 namespaces)

| Tool | Namespace | Key Actions |
|------|-----------|-------------|
| `blueprint_query` | Blueprint (89 actions) | Create/read/modify blueprints, graphs, nodes, variables, functions, components, compile, validate, auto_layout, spawn actors |
| `material_query` | Material (63 actions) | Create/edit materials, expressions, connections, material instances, functions, PBR import, texture preview |
| `animation_query` | Animation (120 actions) | Sequences, montages, blend spaces, notifies, curves, IK/retargeting, control rig, ABP, state machines, pose search |
| `niagara_query` | Niagara (109 actions) | Create systems/emitters, modules, parameters, renderers, data interfaces, compile, preview, HLSL |
| `mesh_query` | Mesh (240 actions) | Mesh info/LODs/collision, scene queries, actor spawning/manipulation, level design, lighting, horror analysis, procedural mesh ops |
| `ai_query` | AI (221 actions) | Behavior trees, state trees, EQS, blackboards, perception, navigation, smart objects, Mass Entity, AI scaffolding |
| `gas_query` | GAS (135 actions) | Abilities, attributes, effects, ASC setup, tags, cues, targeting, input binding, damage pipeline scaffolding |
| `ui_query` | UI (121 actions) | Widget blueprints, UMG, CommonUI, animations, effects, accessibility, settings, input remapping |
| `audio_query` | Audio (98 actions) | Sound cues, MetaSounds, attenuation, concurrency, sound classes, mixes, submixes, batch operations |
| `level_sequence_query` | Level Sequence (8 actions) | Director inspection, bindings, functions, variables |
| `source_query` | C++ Source (11 actions) | Engine source search, class hierarchy, call graph, file reading, project re-indexing |
| `project_query` | Project (7 actions) | Asset search, find references, type search, gameplay tag lookup, asset details |
| `editor_query` | Editor (29 actions) | Build/compile, logs, PIE control, console commands, viewport capture, Python scripting |
| `config_query` | Config (6 actions) | Resolve/explain config settings, diff from defaults, search config files |

## Workflow

### 1. Always Start with Discovery

Before calling any domain action whose parameters are unknown:

```
monolith_discover(namespace="<domain>", category="<optional>")
```

This returns full parameter schemas for every action in that namespace. Never guess parameters.

### 2. Check Status When Things Go Wrong

```
monolith_status()
```

Returns version, uptime, active connections, registered action count, module toggles.

### 3. Blueprint Workflow (the most common)

To modify a blueprint:
1. `blueprint_query(action="get_blueprint_info", params={"asset_path": "/Game/..."})` — inspect first
2. `blueprint_query(action="get_graph_data", ...)` — understand the graph
3. `blueprint_query(action="add_variable" / "add_function" / "add_node" / ...)` — make changes
4. `blueprint_query(action="compile_blueprint", ...)` — compile
5. `blueprint_query(action="validate_blueprint", ...)` — verify

### 4. Search Assets Before Touching

Use `project_query(action="search", params={"query": "damage"})` to find relevant assets.
Use `project_query(action="get_asset_details", ...)` to inspect an asset without modifying it.

### 5. Source Code Lookup

Use `source_query(action="search_source", params={"query": "FCharacterMovement", "limit": 10})` to find engine class definitions.
Use `source_query(action="get_class_hierarchy", params={"class": "AActor", "depth": 3})` to understand inheritance.

### 6. Editor Operations

- `editor_query(action="trigger_build")` — compile C++ in editor
- `editor_query(action="live_compile")` — hot reload
- `editor_query(action="get_build_errors")` — check compile errors
- `editor_query(action="get_recent_logs", params={"lines": 50})` — read editor log
- `editor_query(action="run_console_command", params={"command": "stat fps"})` — execute console commands

### 7. Scaffolding (Quick Boot)

Many domains have `scaffold_*` actions that create complete, connected asset chains:
- `ai_query(action="scaffold_enemy_ai", ...)` — full enemy AI with BT + perception + EQS
- `gas_query(action="scaffold_gas_project", ...)` — entire GAS foundation
- `gas_query(action="scaffold_damage_pipeline", ...)` — damage/heal/status effect pipeline
- `ui_query(action="scaffold_game_user_settings", ...)` — settings menu with save/load
- `ui_query(action="scaffold_input_remapping", ...)` — input rebinding UI

### 8. Batch Operations

Many domains support `batch_execute` for doing multiple actions in one call, reducing round-trips.
Check via `monolith_discover(namespace="<domain>")` if a domain supports batch.

## Asset Path Conventions

- Blueprints: `/Game/Blueprints/BP_MyActor`
- Materials: `/Game/Materials/M_MyMaterial`
- Animations: `/Game/Animations/AM_MyMontage`
- Niagara: `/Game/VFX/NS_MySystem`
- Always use `/Game/` prefix for project content
- Engine content uses `/Engine/` prefix

## Important Restrictions

1. **Monolith only works while UE Editor is open.** If tools fail, check that the editor is running.
2. **Always discover before acting.** Parameter schemas vary by action; never assume.
3. **Prefer `monolith_discover` over reading docs.** It returns live, version-accurate parameter info.
4. **Use `project_query` for asset discovery** before using domain-specific tools.
5. **Some plugins require enabling in Editor Preferences > Plugins > Monolith** (e.g. GAS, Logic Driver, ComboGraph).
