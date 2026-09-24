#!/usr/bin/env python3
"""pack_graph_project.py 镜像 helper 的守卫测试。

scripts/pack_graph_project.py 与 src/project_bundle.cpp 的
RefBasename/UniqueAssetEntry 必须逐语义一致（C++ 是随发布渠道出货的
基准实现）：stem/ext 按 std::filesystem 规则切分（最右点且非首字符才作
扩展名分隔——前导点属文件名、尾点属扩展名）；多冒号引用取首冒号切分、
残留冒号/空名等不干净形态回退 "asset"。本文件与
tests/test_project_bundle.cpp 的 RemapConflictDedupDotfiles 用例共享
相同的期望值，锚定两侧 parity。

运行：python3 tests/test_pack_graph_project.py
"""

import importlib.util
import json
import tempfile
import unittest
import zipfile
from pathlib import Path

_SCRIPT = Path(__file__).resolve().parent.parent / "scripts" / "pack_graph_project.py"
_spec = importlib.util.spec_from_file_location("pack_graph_project", _SCRIPT)
pack_graph_project = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(pack_graph_project)


class RefBasenameTest(unittest.TestCase):
    def test_table(self):
        cases = [
            ("/a/b.png", "b.png"),
            ("/a/.cube", ".cube"),          # 前导点属文件名，原样保留
            ("data/x/y.mnn", "y.mnn"),
            ("C:tmp\\y.png", "y.png"),      # 盘符残留：首冒号后
            ("/x/a:b:c.png", "asset"),      # 残留冒号 → 与 C++ 回退一致
            ("..", "asset"),                # 越界形态本身作基名 → 回退
            ("/x/", "asset"),               # 空基名 → 回退
        ]
        for ref, want in cases:
            self.assertEqual(pack_graph_project.ref_basename(ref), want, ref)


class UniqueAssetEntryTest(unittest.TestCase):
    def dedup(self, names):
        used = {"manifest.json", "g.json"}
        return [pack_graph_project.unique_asset_entry(n, used) for n in names]

    def test_dotfile_conflict(self):
        # 本次修复的分歧点：此前 rpartition 产出 "_2.cube"
        self.assertEqual(self.dedup([".cube", ".cube"]),
                         ["assets/.cube", "assets/.cube_2"])

    def test_plain_conflict(self):
        self.assertEqual(self.dedup(["foo.png", "foo.png", "foo.png"]),
                         ["assets/foo.png", "assets/foo_2.png",
                          "assets/foo_3.png"])

    def test_multi_dot_conflict(self):
        self.assertEqual(self.dedup(["foo.bar.png", "foo.bar.png"]),
                         ["assets/foo.bar.png", "assets/foo.bar_2.png"])

    def test_trailing_dot_conflict(self):
        # 尾点属扩展名（std::filesystem 语义；pathlib 的 suffix 此处返回
        # ''，不能用 pathlib 复刻）
        self.assertEqual(self.dedup(["foo.", "foo."]),
                         ["assets/foo.", "assets/foo_2."])

    def test_double_leading_dot_conflict(self):
        # 最右点（非首字符）仍是扩展名分隔：stem="."、ext=".foo"
        self.assertEqual(self.dedup(["..foo", "..foo"]),
                         ["assets/..foo", "assets/._2.foo"])


class PackDotfileConflictTest(unittest.TestCase):
    """e2e：与 test_project_bundle.cpp 的 RemapConflictDedupDotfiles 同形
    fixture、相同期望值（真正的两侧 parity 锚点）。"""

    def test_pack(self):
        with tempfile.TemporaryDirectory() as td:
            td = Path(td)
            (td / "graphs").mkdir()
            (td / "lut_a").mkdir()
            (td / "lut_b").mkdir()
            lut_a = td / "lut_a" / ".cube"
            lut_b = td / "lut_b" / ".cube"
            lut_a.write_text("LUT_A", encoding="utf-8")
            lut_b.write_text("LUT_B", encoding="utf-8")
            graph = td / "graphs" / "dot_test.json"
            graph.write_text(json.dumps({
                "version": "2.0",
                "tasks": [
                    {"id": "lut1", "type": "render_lut_cube",
                     "params": {"cube_path": str(lut_a)}},
                    {"id": "lut2", "type": "render_lut_cube",
                     "params": {"cube_path": str(lut_b)}},
                ],
                "edges": [],
            }), encoding="utf-8")

            out = td / "dot.tgp"
            self.assertEqual(pack_graph_project.pack(graph, out), 0)

            with zipfile.ZipFile(out) as z:
                self.assertEqual(sorted(z.namelist()), sorted([
                    "manifest.json", "dot_test.json",
                    "assets/.cube", "assets/.cube_2"]))
                man = json.loads(z.read("manifest.json"))
                self.assertEqual([a["path"] for a in man["assets"]],
                                 ["assets/.cube", "assets/.cube_2"])
                self.assertEqual([a["source"] for a in man["assets"]],
                                 [str(lut_a), str(lut_b)])
                # 包内图副本：两个引用分别重写为各自的包内条目
                g = json.loads(z.read("dot_test.json"))
                self.assertEqual([t["params"]["cube_path"] for t in g["tasks"]],
                                 ["assets/.cube", "assets/.cube_2"])
                self.assertEqual(z.read("assets/.cube"), b"LUT_A")
                self.assertEqual(z.read("assets/.cube_2"), b"LUT_B")


if __name__ == "__main__":
    unittest.main(verbosity=2)
