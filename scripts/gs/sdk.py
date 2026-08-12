"""SDK 构建与独立插件编译的共享实现。

对应原 build_sdk.{sh,ps1} 与 build_plugin_standalone.{sh,ps1}，供独立入口脚本和
run_tests.py --sdk（直接函数调用，不走 subprocess）共同复用。
"""

import shutil
from pathlib import Path
from typing import Dict, List, Optional

from . import console
from .cmake import CMake
from .platform import shlib_suffix, is_windows

SDK_DEFAULT_PREFIX = "build/sdk"
SDK_BUILD_DIR = "build/sdk-build"
OUT_ROOT = "build/standalone/plugins"
DEMO_PLUGIN_SRC = "examples/plugins/demo"


def resolve(path_or_none: Optional[str], default: Path) -> Path:
    """None / 空字符串 -> 默认（绝对）；相对路径 -> 相对 cwd 转绝对。"""
    p = Path(path_or_none) if path_or_none else default
    if not p.is_absolute():
        p = Path.cwd() / p
    return p


def opencv_cmake_args(enable: bool, opencv_dir: Optional[Path], hint: Optional[Path]) -> List[str]:
    """构造 OpenCV 的 CMake 变量。

    始终显式传 TASK_GRAPH_ENABLE_OPENCV；仅当能确定 OpenCV 前缀时才额外传 OpenCV_DIR
    （Windows 默认无法自动 find_package，需把前缀目录指给 CMake）。
    """
    args = [f"-DTASK_GRAPH_ENABLE_OPENCV={'ON' if enable else 'OFF'}"]
    if enable:
        prefix = opencv_dir or hint
        if prefix and (prefix / "lib").is_dir():
            args.append(f"-DOpenCV_DIR={prefix / 'lib'}")
    return args


def build_sdk(root: Path, *, prefix: Optional[str] = None, build_dir: Optional[str] = None,
              config: str = "Release", jobs: Optional[int] = None,
              cmake: Optional[Path] = None, disable_opencv: bool = False,
              opencv_dir: Optional[Path] = None, clean: bool = False) -> int:
    """把 task_graph 构建为可分发 SDK 前缀（TASK_GRAPH_BUILD_SUBMODULES=OFF）。

    返回退出码：0 成功，非 0 失败。
    """
    prefix_path = resolve(prefix, root / SDK_DEFAULT_PREFIX)
    build_path = resolve(build_dir, root / SDK_BUILD_DIR)

    # ---- stale-cache 守卫：复用了之前完整构建的目录会让 ON 静默覆盖 OFF ----
    cache_file = build_path / "CMakeCache.txt"
    if not clean and cache_file.is_file():
        try:
            cache_text = cache_file.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            cache_text = ""
        if "TASK_GRAPH_BUILD_SUBMODULES:BOOL=ON" in cache_text:
            console.fail("构建目录已含有非 SDK 缓存 (TASK_GRAPH_BUILD_SUBMODULES=ON)：")
            console.warn(f"  {cache_file}")
            console.warn("  它会静默覆盖本命令的 OFF 并破坏 SDK 配置步骤。")
            console.warn("  请加 --clean 重跑，或用 --build-dir 指定全新目录。")
            return 1

    if clean and build_path.exists():
        console.step("清理 SDK 构建目录")
        shutil.rmtree(build_path, ignore_errors=True)

    c = CMake(cmake)
    defines: List[str] = [
        "-DTASK_GRAPH_BUILD_SUBMODULES=OFF",
        f"-DCMAKE_INSTALL_PREFIX={prefix_path}",
    ]
    defines += opencv_cmake_args(not disable_opencv, opencv_dir, None)

    code = c.configure(root, build_path, defines=defines, build_type=None if is_windows() else config)
    if code != 0:
        return code
    code = c.build(build_path, config=config, jobs=jobs, target="task_graph", what="构建 libtask_graph")
    if code != 0:
        return code
    code = c.install(build_path, config=config)
    if code != 0:
        return code

    console.ok(f"SDK 就绪: {prefix_path}")
    cfg_dir = prefix_path / "lib" / "cmake" / "task_graph"
    print(f"  独立编译插件时配置: -Dtask_graph_DIR={cfg_dir}")
    return 0


def plugin_build_dirs(lib_build: Path, config: str) -> List[Path]:
    """收集包含插件动态库的目录，用于设置 TASK_GRAPH_PLUGINS_PATH。

    Windows（多配置）布局: build/submodules/<plugin>/<config>/ 含 .dll；
    Unix（单配置）布局:     build/submodules/<plugin>/ 直接含 .so/.dylib。
    """
    suffix = shlib_suffix()
    result: List[Path] = []
    root = lib_build / "submodules"
    if not root.is_dir():
        return result
    for plugin in sorted(root.iterdir()):
        if not plugin.is_dir():
            continue
        candidates = [plugin / config, plugin]
        for d in candidates:
            if not d.is_dir():
                continue
            if any(p.is_file() and p.name.endswith(suffix) for p in d.iterdir()):
                result.append(d)
                break
    return result


def find_plugin_product(out_dir: Path, config: str, prefer_name: str) -> Optional[Path]:
    """在多配置 (<config> 子目录) 与单配置（平铺）布局里找插件动态库。"""
    suffix = shlib_suffix()
    dirs = [out_dir / config, out_dir]
    candidates: List[Path] = []
    for d in dirs:
        if not d.is_dir():
            continue
        for p in d.iterdir():
            if p.is_file() and p.name.endswith(suffix):
                candidates.append(p)
    if not candidates:
        return None
    for p in candidates:
        if p.stem == prefer_name:
            return p
    return sorted(candidates)[0]


def build_plugin_standalone(root: Path, src_dir: str, *, sdk_dir: Optional[str] = None,
                            out_root: Optional[str] = None, config: str = "Release",
                            jobs: Optional[int] = None, cmake: Optional[Path] = None,
                            enable_opencv: bool = False, clean: bool = False) -> Dict[str, object]:
    """独立编译一个插件（仅依赖 SDK 前缀）为运行时动态库。

    返回 dict：{code, name, out_dir, product(Path|None)}。code 为配置/构建的退出码。
    """
    src = Path(src_dir)
    if not src.is_absolute():
        src = Path.cwd() / src
    sdk_path = resolve(sdk_dir, root / SDK_DEFAULT_PREFIX)
    out_root_path = resolve(out_root, root / OUT_ROOT)

    if not (src / "CMakeLists.txt").is_file():
        console.fail(f"错误: {src} 下没有 CMakeLists.txt")
        return {"code": 1, "name": src.name, "out_dir": out_root_path / src.name, "product": None}

    cfg_file = sdk_path / "lib" / "cmake" / "task_graph" / "task_graphConfig.cmake"
    if not cfg_file.is_file():
        console.fail(f"错误: 未找到 SDK 包 {cfg_file}")
        console.warn("  请先运行 scripts/build_sdk.py")
        return {"code": 1, "name": src.name, "out_dir": out_root_path / src.name, "product": None}

    name = src.name
    out_dir = out_root_path / name

    if clean and out_dir.exists():
        console.step("清理插件构建目录")
        shutil.rmtree(out_dir, ignore_errors=True)

    c = CMake(cmake)
    defines: List[str] = [
        f"-Dtask_graph_DIR={sdk_path / 'lib' / 'cmake' / 'task_graph'}",
        f"-DTASK_GRAPH_ENABLE_OPENCV={'ON' if enable_opencv else 'OFF'}",
    ]
    code = c.configure(src, out_dir, defines=defines, build_type=None if is_windows() else config)
    if code != 0:
        return {"code": code, "name": name, "out_dir": out_dir, "product": None}
    code = c.build(out_dir, config=config, jobs=jobs, what=f"构建 {name}")
    if code != 0:
        return {"code": code, "name": name, "out_dir": out_dir, "product": None}

    product = find_plugin_product(out_dir, config, name)
    console.ok(f"插件产物: {out_dir}")
    print("  dlopen 该动态库即可加载（见 PluginLoader / test_plugin_abi）")
    if product:
        console.ok(f"动态库: {product}")
    return {"code": 0, "name": name, "out_dir": out_dir, "product": product}