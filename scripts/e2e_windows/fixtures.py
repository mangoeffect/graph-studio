"""fixtures.py — 自包含 E2E 夹具：graph.json（version 2.0）+ 现场生成的小 PNG。

不依赖仓库 submodule 布局，任何安装态 GraphStudio 都能打开执行：
  graphs/blur_graph.json   src(opencv_image_read, file_path=assets/test.png) → blur(gaussian)
  graphs/drop_graph.json   同结构、不同 id/文件名，专供拖拽打开用
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass
from pathlib import Path


def make_png(path: Path, width: int = 16, height: int = 16, rgb: tuple = (184, 62, 44)):
    """stdlib 构造一张纯色 PNG（8-bit truecolor）。"""
    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    row = b"\x00" + bytes(rgb) * width           # filter 0 + RGB 扫描行
    raw = row * height
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
                     chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
    return path


def _blur_graph(src_id: str, blur_id: str, rel_asset: str) -> dict:
    return {
        "version": "2.0",
        "tasks": [
            {"id": src_id, "type": "opencv_image_read",
             "params": {"file_path": rel_asset}},
            {"id": blur_id, "type": "opencv_gaussian_blur_filter",
             "params": {"kernel_size": 5, "sigma": 0.0}},
        ],
        "edges": [
            {"from": src_id, "from_port": "out", "to": blur_id, "to_port": "in"},
        ],
    }


@dataclass
class Fixtures:
    root: Path                 # 夹具根目录
    blur_graph: Path           # File>Open 用
    drop_graph: Path           # WM_DROPFILES 用
    asset_png: Path            # 相对路径资产

    @property
    def asset_abs(self) -> str:
        return str(self.asset_png.resolve())


def make_fixtures(root: Path) -> Fixtures:
    graphs = root / "graphs"
    fx = Fixtures(root=graphs,
                  blur_graph=graphs / "blur_graph.json",
                  drop_graph=graphs / "drop_graph.json",
                  asset_png=graphs / "assets" / "test.png")
    make_png(fx.asset_png)
    fx.blur_graph.write_text(_json(_blur_graph("src", "blur", "assets/test.png")),
                            encoding="utf-8")
    fx.drop_graph.write_text(_json(_blur_graph("source2", "smooth2", "assets/test.png")),
                            encoding="utf-8")
    return fx


def _json(obj: dict) -> str:
    import json
    return json.dumps(obj, indent=2, ensure_ascii=False)
