#!/usr/bin/env python3
"""
Convert a STEP/STP file into one STL per solid plus a manifest.json.

The GUI intentionally calls this as an external command so the STEP kernel can
be replaced or packaged independently on macOS/Windows. The default
implementation uses OCP, which is bundled by CadQuery wheels.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path


def decode_step_string(text: str) -> str:
    """Decode common STEP escaped unicode fragments such as \\X2\\5B9E4F53\\X0\\."""

    def repl(match: re.Match[str]) -> str:
        hex_text = re.sub(r"\s+", "", match.group(1))
        try:
            return bytes.fromhex(hex_text).decode("utf-16-be")
        except Exception:
            return match.group(0)

    return re.sub(r"\\X2\\([0-9A-Fa-f]+)\\X0\\", repl, text)


def solid_names_from_step(step_path: Path) -> list[str]:
    try:
        data = step_path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return []

    names: list[str] = []
    for raw_name in re.findall(r"MANIFOLD_SOLID_BREP\('([^']*)'", data):
        decoded = decode_step_string(raw_name).strip()
        names.append(decoded or f"SOLID-{len(names) + 1}")
    return names


def safe_filename(name: str, fallback: str) -> str:
    cleaned = re.sub(r"[^\w.\-\u4e00-\u9fff]+", "_", name, flags=re.UNICODE).strip("._")
    return cleaned or fallback


def convert_with_ocp(input_path: Path, output_dir: Path, linear_deflection: float) -> dict:
    try:
        from OCP.BRepMesh import BRepMesh_IncrementalMesh
        from OCP.IFSelect import IFSelect_RetDone
        from OCP.STEPControl import STEPControl_Reader
        from OCP.StlAPI import StlAPI_Writer
        from OCP.TopAbs import TopAbs_SOLID
        from OCP.TopExp import TopExp_Explorer
    except Exception as exc:  # pragma: no cover - depends on local CAD kernel
        raise RuntimeError(
            "缺少 OCP/CadQuery STEP 几何内核。请先安装 cadquery，或打包 step_to_stl_parts 可执行文件。"
        ) from exc

    reader = STEPControl_Reader()
    status = reader.ReadFile(str(input_path))
    if status != IFSelect_RetDone:
        raise RuntimeError(f"无法读取 STEP 文件: {input_path}")

    transferred = reader.TransferRoots()
    if transferred <= 0:
        raise RuntimeError("STEP 文件没有可转换的根形状。")

    shape = reader.OneShape()
    BRepMesh_IncrementalMesh(shape, linear_deflection).Perform()

    output_dir.mkdir(parents=True, exist_ok=True)
    writer = StlAPI_Writer()
    try:
        writer.SetASCIIMode(False)
    except AttributeError:
        pass

    preferred_names = solid_names_from_step(input_path)
    parts = []
    explorer = TopExp_Explorer(shape, TopAbs_SOLID)
    index = 1
    while explorer.More():
        solid = explorer.Current()
        display_name = preferred_names[index - 1] if index - 1 < len(preferred_names) else f"SOLID-{index}"
        if not display_name.upper().startswith("SOLID"):
            display_name = f"{display_name}"
        stl_name = f"{index:03d}_{safe_filename(display_name, f'SOLID_{index}')}.stl"
        stl_path = output_dir / stl_name
        BRepMesh_IncrementalMesh(solid, linear_deflection).Perform()
        result = writer.Write(solid, str(stl_path))
        if result is False or not stl_path.exists():
            raise RuntimeError(f"无法写出 STL: {stl_path}")
        parts.append(
            {
                "name": display_name,
                "stl": stl_name,
                "path": [input_path.name, display_name],
            }
        )
        index += 1
        explorer.Next()

    if not parts:
        raise RuntimeError("STEP 文件没有找到可导出的 SOLID 实体。")

    return {"assembly": input_path.name, "parts": parts}


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Convert STEP solids to STL parts.")
    parser.add_argument("--input", required=True, help="Path to .step/.stp file")
    parser.add_argument("--output", required=True, help="Output directory")
    parser.add_argument("--linear-deflection", type=float, default=0.08, help="OCC meshing deflection in mm")
    args = parser.parse_args(argv)

    input_path = Path(args.input).expanduser().resolve()
    output_dir = Path(args.output).expanduser().resolve()
    if not input_path.exists():
        print(f"输入 STEP 文件不存在: {input_path}", file=sys.stderr)
        return 2

    try:
        manifest = convert_with_ocp(input_path, output_dir, args.linear_deflection)
        manifest_path = output_dir / "manifest.json"
        manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"STEP 转换完成: {len(manifest['parts'])} 个实体 -> {output_dir}")
        return 0
    except Exception as exc:
        print(f"STEP 转换失败: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
