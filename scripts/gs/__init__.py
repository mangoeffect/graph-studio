"""task_graph 跨平台开发脚本的共享工具包（纯标准库，Python 3.9+）。

统一承载原先分散在多个 .sh / .ps1 里的公共逻辑：
  - console   颜色输出（step/ok/fail/warn，遵循 NO_COLOR）
  - runner    subprocess 包装（等价 PowerShell 的 Invoke-Native）
  - platform  OS 判断、CPU 核数、动态库后缀、运行时搜索路径注入、特性宏
  - toolchain cmake/ctest/Windows SDK 工具发现（含 VS2022 自带 cmake 兜底）
  - deps      Qt / OpenCV 跨平台探测
  - cmake     CMake 单/多配置生成器抽象（configure/build/install/ctest）
  - sdk       build_sdk / build_plugin_standalone 的共享实现
"""

from pathlib import Path


def repo_root() -> Path:
    """仓库根目录 = <root>/scripts/gs/ 的上三级。"""
    return Path(__file__).resolve().parents[2]