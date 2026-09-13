# -*- coding: utf-8 -*-
"""e2e_android — Android 端 graph E2E 驱动包（adb + tg_e2e_runner）。

零第三方依赖：纯标准库。adb 进程走 os.posix_spawn（对齐 run_e2e_macos.py 的
AppRunner 手法），设备命令一律 argv 列表传递、不拼 shell 字符串。

与另两端的关系：图发现/落位/裁剪（e2e_graph_cases.py）与报告（e2e_report.py）
是 scripts/ 层的三端共享模块；本包只承载 Android 特有的部分——adb 会话
（adb.py）与场景语义（scenarios.py：boot/run/files/crash）。

前置与用法见 scripts/run_e2e_android.py 的模块 docstring 与
dev-docs/e2e-android.md。
"""
