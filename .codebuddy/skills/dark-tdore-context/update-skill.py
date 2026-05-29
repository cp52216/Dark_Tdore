#!/usr/bin/env python3
"""
Dark_Tdore 项目上下文 Skill 增量更新脚本

特性：
  - 基于 git diff 增量检测变更（非全盘扫描）
  - 跟踪所有已提交的文件变更（Source/Content/Config/Plugins/Docs 等）
  - 回退代码（git revert）同样触发增量更新
  - 首次提交自动退化为全量扫描

触发方式：
  1. Git post-commit hook (自动)
  2. 手动: python update-skill.py
"""

import os
import re
import subprocess
import sys
from datetime import datetime
from pathlib import Path

# 路径常量
SCRIPT_DIR = Path(__file__).parent.resolve()
PROJECT_ROOT = SCRIPT_DIR.parent.parent.parent.resolve()
SKILL_FILE = SCRIPT_DIR / "SKILL.md"
SOURCE_DIR = PROJECT_ROOT / "Source" / "Dark_Tdore"
PLUGINS_DIR = PROJECT_ROOT / "Plugins"
CONFIG_DIR = PROJECT_ROOT / "Config"
CONTENT_DIR = PROJECT_ROOT / "Content"
DOCS_DIR = PROJECT_ROOT / "Docs"

# 排除路径：仅忽略 .git 内部文件
IGNORE_PATHS = [
    ".git/",
]


def git_diff_files() -> list[str] | None:
    """获取最近一次提交的全部变更文件列表（不过滤路径）。
    返回 None 表示无法增量（首次提交），应走全量扫描。
    """
    try:
        result = subprocess.run(
            ["git", "diff", "--name-only", "HEAD~1", "HEAD"],
            capture_output=True, text=True,
            cwd=str(PROJECT_ROOT),
            timeout=10,
        )
        if result.returncode != 0:
            return None

        raw = result.stdout.strip()
        if not raw:
            # 可能是首次提交
            result2 = subprocess.run(
                ["git", "diff", "--name-only", "--diff-filter=A", "HEAD"],
                capture_output=True, text=True,
                cwd=str(PROJECT_ROOT),
                timeout=10,
            )
            if result2.stdout.strip():
                return _filter_ignore(result2.stdout.strip())
            return None

        return _filter_ignore(raw)
    except Exception as e:
        print(f"[增量] git diff 失败: {e}，退化为全量扫描")
        return None


def _filter_ignore(raw_output: str) -> list[str]:
    """过滤排除路径，返回干净的变更列表"""
    files = [f.strip().replace("\\", "/") for f in raw_output.split("\n") if f.strip()]
    filtered = [f for f in files if not any(f.startswith(ig) for ig in IGNORE_PATHS)]
    return filtered


def scan_directory(dir_path: Path, pattern: str = "*.h") -> list[str]:
    """递归扫描目录下匹配模式的文件，返回相对 dir_path 的路径列表"""
    if not dir_path.exists():
        return []
    return sorted(str(f.relative_to(dir_path).as_posix()) for f in dir_path.rglob(pattern))


def scan_top_dirs(dir_path: Path, max_depth: int = 2) -> list[dict]:
    """扫描目录顶层子目录结构"""
    if not dir_path.exists():
        return []
    result = []
    for entry in sorted(dir_path.iterdir()):
        if entry.name.startswith(".") or entry.name.startswith("__"):
            continue
        if not entry.is_dir():
            continue
        item = {
            "name": entry.name,
            "is_dir": True,
        }
        # 统计子文件数
        all_files = list(entry.rglob("*"))
        files_only = [f for f in all_files if f.is_file()]
        item["file_count"] = len(files_only)
        # 获取文件扩展名统计
        ext_counts: dict[str, int] = {}
        for f in files_only:
            ext = f.suffix if f.suffix else "(none)"
            ext_counts[ext] = ext_counts.get(ext, 0) + 1
        item["extensions"] = ext_counts
        # 子目录
        subdirs = [s.name for s in sorted(entry.iterdir()) if s.is_dir() and not s.name.startswith("__")]
        item["subdirs"] = subdirs[:15]
        result.append(item)
    return result


def extract_classes_from_file(filepath: Path) -> list[str]:
    """从单个头文件中提取 UCLASS/USTRUCT/UENUM 类名"""
    classes = []
    try:
        content = filepath.read_text(encoding="utf-8", errors="ignore")
        for match in re.finditer(
            r'(?:UCLASS|USTRUCT|UENUM)\b.*?\bclass\s+(?:\w+_API\s+)?(\w+)',
            content,
            re.DOTALL,
        ):
            classes.append(match.group(1))
    except Exception:
        pass
    return classes


def update_skill_file_full() -> bool:
    """全量扫描并更新 SKILL.md（首次或兜底）"""
    if not SKILL_FILE.exists():
        print(f"SKILL.md 不存在: {SKILL_FILE}")
        return False

    now = datetime.now().strftime("%Y-%m-%d %H:%M")
    content = SKILL_FILE.read_text(encoding="utf-8")

    # 统计各个目录
    h_files = scan_directory(SOURCE_DIR, "*.h")
    cpp_files = scan_directory(SOURCE_DIR, "*.cpp")
    cs_files = scan_directory(SOURCE_DIR, "*.cs")
    content_dirs = scan_top_dirs(CONTENT_DIR) if CONTENT_DIR.exists() else []

    # 更新时间戳
    content = re.sub(
        r'\*\*最后更新\*\*: .+',
        f'**最后更新**: {now} (全量扫描)',
        content,
    )

    # 尝试更新 Content 目录结构章节
    content = _update_content_section(content, content_dirs, now)

    log_entry = f"- **{now}**: 全量扫描，{len(h_files)} .h + {len(cpp_files)} .cpp + {len(cs_files)} .cs, Content {len(content_dirs)} 顶层目录\n"
    content = append_update_log(content, log_entry)

    SKILL_FILE.write_text(content, encoding="utf-8")
    print(f"[全量] SKILL.md 已更新 ({now})")
    return True


def update_skill_file_incremental(changed_files: list[str]) -> bool:
    """增量更新 SKILL.md —— 处理所有变更文件"""
    if not SKILL_FILE.exists():
        print(f"SKILL.md 不存在: {SKILL_FILE}")
        return False

    now = datetime.now().strftime("%Y-%m-%d %H:%M")
    content = SKILL_FILE.read_text(encoding="utf-8")

    # 按模块分类变更
    changed_h = []
    changed_cpp = []
    changed_cs = []
    changed_plugin = []
    changed_config = []
    changed_content = []
    changed_docs = []
    changed_uproject = []
    changed_other = []

    for f in changed_files:
        norm = f.replace("\\", "/")
        if norm.startswith("Source/Dark_Tdore/"):
            if norm.endswith(".h"):
                changed_h.append(norm)
            elif norm.endswith(".cpp"):
                changed_cpp.append(norm)
            elif norm.endswith(".cs"):
                changed_cs.append(norm)
            else:
                changed_other.append(norm)
        elif norm.startswith("Content/"):
            changed_content.append(norm)
        elif norm.startswith("Plugins/") and norm.endswith(".uplugin"):
            changed_plugin.append(norm)
        elif norm.startswith("Config/"):
            changed_config.append(norm)
        elif norm.startswith("Docs/"):
            changed_docs.append(norm)
        elif norm.endswith(".uproject"):
            changed_uproject.append(norm)
        else:
            changed_other.append(norm)

    # 从变更的头文件提取类名
    new_classes: dict[str, list[str]] = {}
    for f_rel in changed_h:
        abs_path = PROJECT_ROOT / f_rel
        if abs_path.exists():
            classes = extract_classes_from_file(abs_path)
            if classes:
                group = abs_path.parent.name
                new_classes.setdefault(group, []).extend(classes)

    # ── 构建变更摘要 ──
    summary_parts = []
    if changed_h:
        summary_parts.append(f"{len(changed_h)} .h")
    if changed_cpp:
        summary_parts.append(f"{len(changed_cpp)} .cpp")
    if changed_cs:
        summary_parts.append(f"{len(changed_cs)} .cs")
    if changed_content:
        summary_parts.append(f"{len(changed_content)} Content")
    if changed_plugin:
        summary_parts.append(f"{len(changed_plugin)} .uplugin")
    if changed_config:
        summary_parts.append(f"{len(changed_config)} .ini")
    if changed_docs:
        summary_parts.append(f"{len(changed_docs)} Docs")
    if changed_uproject:
        summary_parts.append(f".uproject")
    change_summary = " + ".join(summary_parts) if summary_parts else "其他文件"

    # 1. 更新时间戳
    content = re.sub(
        r'\*\*最后更新\*\*: .+',
        f'**最后更新**: {now} (增量: {change_summary})',
        content,
    )

    # 2. 追加详细更新日志
    log_lines = [f"- **{now}**: 增量更新 ({change_summary})"]
    for label, files in [
        ("Source/.h", changed_h), ("Source/.cpp", changed_cpp), ("Source/.cs", changed_cs),
        ("Content", changed_content), ("Plugins/.uplugin", changed_plugin),
        ("Config/.ini", changed_config), ("Docs", changed_docs),
        (".uproject", changed_uproject), ("Other", changed_other),
    ]:
        if files:
            preview = ", ".join(files[:10])
            if len(files) > 10:
                preview += f" ... 共{len(files)}个"
            log_lines.append(f"  - {label}: {preview}")
    if new_classes:
        for group, clist in new_classes.items():
            log_lines.append(f"  - 类变更 [{group}]: {', '.join(clist)}")
    deleted = [f for f in changed_files if not (PROJECT_ROOT / f).exists()]
    if deleted:
        log_lines.append(f"  - 已删除: {', '.join(deleted[:10])}")
    log_entry = "\n".join(log_lines) + "\n"

    content = append_update_log(content, log_entry)

    # 3. 尝试在 SKILL.md 中补充新类名
    if new_classes:
        content = _update_class_sections(content, new_classes)

    # 4. 全量刷新 Content 目录统计（每次增量都更新统计，保持准确）
    content_dirs = scan_top_dirs(CONTENT_DIR) if CONTENT_DIR.exists() else []
    if content_dirs:
        content = _update_content_section(content, content_dirs, now)

    SKILL_FILE.write_text(content, encoding="utf-8")
    print(f"[增量] SKILL.md 已更新 ({now}), 变更: {change_summary}")
    return True


def _update_content_section(content: str, dirs: list[dict], now: str) -> str:
    """更新 Content/ 目录结构的 Markdown 章节"""
    section_marker = "## Content 目录结构"
    lines = ["## Content 目录结构", ""]
    lines.append(f"> 自动生成于 {now}")
    lines.append("")
    lines.append("| 目录 | 文件数 | 类型 | 子目录 |")
    lines.append("|------|--------|------|--------|")

    for d in dirs:
        ext_summary = ", ".join(
            f"{ext}({cnt})" for ext, cnt in sorted(d["extensions"].items(), key=lambda x: -x[1])[:3]
        )
        subs = ", ".join(d.get("subdirs", [])[:8])
        if len(d.get("subdirs", [])) > 8:
            subs += " ..."
        lines.append(f"| {d['name']} | {d['file_count']} | {ext_summary or '-'} | {subs or '-'} |")

    new_section = "\n".join(lines) + "\n"

    if section_marker in content:
        # 替换已有章节
        content = re.sub(
            r'## Content 目录结构.*?(?=\n## |\n---\n|\Z)',
            new_section.rstrip(),
            content,
            flags=re.DOTALL,
        )
    else:
        # 插入到目录结构章节之后
        insert_after = "## 二、目录结构"
        if insert_after in content:
            # 找到下一个 ## 章节之前插入
            idx = content.find(insert_after)
            next_section = content.find("\n## ", idx + len(insert_after) + 1)
            if next_section == -1:
                next_section = content.find("\n---", idx)
            if next_section == -1:
                content = content.rstrip() + "\n\n" + new_section
            else:
                content = content[:next_section].rstrip() + "\n\n" + new_section + "\n" + content[next_section:]

    return content


def _update_class_sections(content: str, new_classes: dict[str, list[str]]) -> str:
    """在目录结构中为新增类追加条目"""
    for group, classes in new_classes.items():
        for cls in classes:
            if cls in content:
                continue
            group_pattern = rf"(│   ├── {group}/\s*\n)((?:│   │   .*\n)*)"
            match = re.search(group_pattern, content)
            if match:
                insert_line = f"│   │   ├── {cls}                              # 新增\n"
                content = content.replace(match.group(0), match.group(1) + match.group(2) + insert_line)
    return content


def append_update_log(content: str, new_entry: str) -> str:
    """追加更新日志，保留最近 20 条"""
    log_section_start = "## 自动更新日志"
    existing_logs = re.findall(r'^- \*\*.+?\*\*: .+$', content, re.MULTILINE)

    if log_section_start not in content:
        content += f"\n\n---\n\n## 自动更新日志\n\n{new_entry}"
        return content

    content = re.sub(r'\n---\n## 自动更新日志.*', '', content, flags=re.DOTALL)

    trimmed = existing_logs[-19:] if len(existing_logs) > 19 else existing_logs
    log_text = "\n".join(trimmed) + "\n" + new_entry if trimmed else new_entry

    content += f"\n---\n## 自动更新日志\n\n{log_text}"
    return content


def main():
    print("=== Dark_Tdore Skill 增量更新脚本 ===")
    print(f"项目根目录: {PROJECT_ROOT}")

    if not PROJECT_ROOT.exists():
        print("错误: 项目根目录不存在")
        return 1

    changed = git_diff_files()

    if changed is None:
        print("[模式] 全量扫描 (无法增量检测)")
        return 0 if update_skill_file_full() else 1
    elif not changed:
        print("[模式] 无变更，跳过")
        return 0
    else:
        print(f"[模式] 增量更新 ({len(changed)} 个变更文件)")
        for f in changed[:20]:
            print(f"  - {f}")
        if len(changed) > 20:
            print(f"  ... 共 {len(changed)} 个文件")
        return 0 if update_skill_file_incremental(changed) else 1


if __name__ == "__main__":
    sys.exit(main())
