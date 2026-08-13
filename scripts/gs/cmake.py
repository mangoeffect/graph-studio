"""CMake 生成器抽象：统一 configure / build / install / ctest 调用。

关键平台差异：
  - Unix 单配置生成器：configure 传 -DCMAKE_BUILD_TYPE，build/install 不带 --config
  - Windows VS 多配置生成器：不传 -DCMAKE_BUILD_TYPE，build/install 带 --config，
    ctest 带 -C <config>
"""

from pathlib import Path
from typing import List, Optional, Sequence, Union

from . import console, runner
from .platform import multi_config_generator

Arg = Union[str, Path, int]


class CMake:
    def __init__(self, cmake: Path, multi_config: Optional[bool] = None):
        self.cmake = Path(cmake)
        self.multi_config = multi_config_generator() if multi_config is None else multi_config

    # ---- 内部参数构造 ----

    def _config_flags(self, config: Optional[str] = None) -> List[Arg]:
        if self.multi_config and config:
            return ["--config", config]
        return []

    def _build_type_flag(self, build_type: Optional[str] = None) -> List[Arg]:
        if not self.multi_config and build_type:
            return [f"-DCMAKE_BUILD_TYPE={build_type}"]
        return []

    # ---- 公开操作 ----

    def configure(self, src: Path, build: Path, defines: Sequence[str] = (),
                  extra: Sequence[Arg] = (), build_type: Optional[str] = None) -> int:
        args: List[Arg] = [self.cmake, "-S", str(src), "-B", str(build)]
        args += self._build_type_flag(build_type)
        args += [d for d in defines]
        args += [a for a in extra]
        console.step(f"配置 (cmake {' '.join(str(a) for a in args[1:])})")
        code = runner.check(args, what="配置")
        return code

    def build(self, build: Path, config: Optional[str] = None, jobs: Optional[int] = None,
              target=None, what: str = "构建") -> int:
        args: List[Arg] = [self.cmake, "--build", str(build)]
        if target:
            targets = [target] if isinstance(target, str) else list(target)
            args.append("--target")
            args += targets
        args += self._config_flags(config)
        if jobs:
            args += ["-j", jobs]
        console.step(f"{what} (cmake {' '.join(str(a) for a in args[1:])})")
        return runner.check(args, what=what)

    def install(self, build: Path, config: Optional[str] = None, what: str = "安装") -> int:
        args: List[Arg] = [self.cmake, "--install", str(build)]
        args += self._config_flags(config)
        console.step(f"{what} (cmake {' '.join(str(a) for a in args[1:])})")
        return runner.check(args, what=what)

    def ctest(self, ctest: Path, build: Path, config: Optional[str] = None,
              filter: Optional[str] = None, exclude: Optional[str] = None,
              verbose: bool = False, list_only: bool = False) -> int:
        args: List[Arg] = [ctest]
        if self.multi_config and config:
            args.append("-C")
            args.append(config)
        if list_only:
            args.append("-N")
        else:
            args.append("--output-on-failure")
            if filter:
                args += ["-R", filter]
            if exclude:
                args += ["-E", exclude]
            if verbose:
                args.append("-V")
        label = "列出测试" if list_only else "运行测试"
        console.step(f"{label} (ctest {' '.join(str(a) for a in args[1:])})")
        return runner.check(args, cwd=str(build), what=label)