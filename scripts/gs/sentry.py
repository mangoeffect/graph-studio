"""gs.sentry — sentry-native 拉取辅助（package_macos / package_linux 打包脚本共用）。

sentry-native 及其 crashpad 子模块体积较大且被 gitignore，由 fetch_sentry.py
按固定版本克隆到 app/graph_studio/third_party/sentry-native；打包脚本在启用
崩溃上报时调用 ensure_fetched() 幂等确保就位（已存在则直接跳过）。
"""

import sys
from pathlib import Path

from gs import console, runner


def ensure_fetched(root: Path, enable: bool = True) -> int:
    """启用 Sentry 时确保 sentry-native 已拉取（幂等）。返回退出码，0 为就绪。

    enable=False 时直接返回 0（等价 --no-sentry：构建无崩溃上报能力）。
    """
    if not enable:
        return 0
    sentry_root = root / "app" / "graph_studio" / "third_party" / "sentry-native"
    if (sentry_root / "CMakeLists.txt").is_file():
        return 0
    console.step("拉取 sentry-native（首次较慢）")
    return runner.run([sys.executable, str(root / "scripts" / "fetch_sentry.py")])


def cmake_defines(dsn: str = "", release: str = "") -> list:
    """生成传给 app/graph_studio cmake configure 的 Sentry 相关 -D 参数。

    - dsn：编译期嵌入 DSN（公开 client key，可安全进包；运行期 SENTRY_DSN
      环境变量优先级更高）。
    - release：完整渠道版本（如 0.1.0-beta.42），使 Sentry release 与 GitHub
      发布 tag 一致；缺省时 CMake 侧取根 project VERSION。
    """
    defines = []
    if dsn:
        defines.append(f"-DGRAPH_STUDIO_SENTRY_DSN={dsn}")
    if release:
        defines.append(f"-DGRAPH_STUDIO_SENTRY_VERSION={release}")
    return defines
