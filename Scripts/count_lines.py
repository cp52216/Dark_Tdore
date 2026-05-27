#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
项目代码行数统计工具
用法: python Scripts/count_lines.py [--json] [--stats]
"""

import os, sys, json
from pathlib import Path
from collections import defaultdict

SCRIPT_DIR = Path(__file__).parent.resolve()
ROOT = SCRIPT_DIR.parent

SOURCE_DIR = ROOT / "Source"
CONTENT_DIR = ROOT / "Content"
CPP_EXTS = {".h", ".hpp", ".cpp", ".c", ".inl"}


def count_file_lines(filepath):
    """返回 (总行, 代码行, 空行)"""
    total = code = blank = 0
    in_block = False
    try:
        with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                total += 1
                s = line.strip()
                if not s:
                    blank += 1
                    continue
                if in_block:
                    if "*/" in s:
                        in_block = False
                    continue
                if s.startswith("/*"):
                    in_block = "*/" not in s
                    continue
                if s.startswith("//"):
                    continue
                code += 1
    except:
        pass
    return total, code, blank


def main():
    data = defaultdict(lambda: {"files": 0, "total": 0, "code": 0, "blank": 0})
    detail = defaultdict(lambda: defaultdict(lambda: {"files": 0, "total": 0, "code": 0}))

    for root, dirs, files in os.walk(SOURCE_DIR):
        for f in files:
            ext = Path(f).suffix.lower()
            if ext not in CPP_EXTS:
                continue
            fp = Path(root) / f
            total, code, blank = count_file_lines(fp)
            rel = fp.relative_to(SOURCE_DIR)
            parts = rel.parts

            # 模块 = parts[1]（如 AbilitySystem, Character）
            mod = parts[1] if len(parts) > 1 else parts[0]
            data[mod]["files"] += 1
            data[mod]["total"] += total
            data[mod]["code"] += code
            data[mod]["blank"] += blank

            # 子模块 = parts[2]（如 Abilities, Attributes, Executions）
            sub = parts[2] if len(parts) > 2 else "(root)"
            detail[mod][sub]["files"] += 1
            detail[mod][sub]["total"] += total
            detail[mod][sub]["code"] += code

    uasset, umap = 0, 0
    if CONTENT_DIR.exists():
        for root, dirs, files in os.walk(CONTENT_DIR):
            for f in files:
                if f.endswith(".uasset"):
                    uasset += 1
                elif f.endswith(".umap"):
                    umap += 1

    total_files = sum(d["files"] for d in data.values())
    total_lines = sum(d["total"] for d in data.values())
    total_code = sum(d["code"] for d in data.values())
    total_blank = sum(d["blank"] for d in data.values())

    if "--json" in sys.argv:
        out = {cat: dict(d) for cat, d in data.items()}
        out["content"] = {"uasset": uasset, "umap": umap}
        print(json.dumps(out, ensure_ascii=False, indent=2))
        return

    print()
    print(f"  ========================================================")
    print(f"  Dark_Tdore Project Stats")
    print(f"  ========================================================")
    print(f"  C++ Source:   {total_files:>4} files")
    print(f"                {total_lines:>6,} lines (with comments/blank)")
    print(f"                {total_code:>6,} pure code (no comments/blank)")
    print(f"  Blueprint:    {uasset:>4} .uasset, {umap:>4} .umap")
    print(f"  --------------------------------------------------------")
    print(f"  C++ TOTAL:    {total_code:,} lines of pure C++ code")
    print(f"  ========================================================")
    print(f"\n  Modules:")
    for mod in sorted(data.keys()):
        d = data[mod]
        print(f"    {mod:<30} {d['files']:>4} files  {d['total']:>6} lines  {d['code']:>5} code")
        for sub in sorted(detail[mod].keys()):
            sd = detail[mod][sub]
            if sd["files"] > 0:
                print(f"      {sub:<28} {sd['files']:>4} files  {sd['total']:>6} lines  {sd['code']:>5} code")

    if "--stats" in sys.argv:
        md = ROOT / "Docs" / "Project_Stats.md"
        md.parent.mkdir(exist_ok=True)
        with open(md, "w", encoding="utf-8") as f:
            f.write(f"# Project Code Stats\n\n")
            f.write(f"| Module | Files | Lines | Code |\n|--------|-------|-------|------|\n")
            for cat in sorted(data.keys()):
                d = data[cat]
                f.write(f"| {cat} | {d['files']} | {d['total']} | {d['code']} |\n")
            f.write(f"\n**Source:** {total_files} files, {total_lines:,} lines, {total_code:,} code\n")
            f.write(f"\n**Content:** {uasset:,} .uasset, {umap:,} .umap\n")
        print(f"  -> {md}")


if __name__ == "__main__":
    main()
