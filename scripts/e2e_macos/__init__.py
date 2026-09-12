# -*- coding: utf-8 -*-
"""e2e_macos — macOS 安装态 GraphStudio 的 AXAPI + CGEvent E2E 驱动包。

零第三方依赖：ctypes 直调 ApplicationServices（AX 控件树）/ CoreGraphics
（CGEvent 注入）。同 pyobjc/atomacos 走的是同一套 AXAPI 协议，此处自制薄
封装是因为新版 macOS 的各 Python 发行版均不再预装 pyobjc，且 PEP 668
环境下 pip 装包对 repo 工具不友好。

权限：AX 读取与 CGEvent 注入都需要运行方（终端/IDE）拥有
「系统设置 -> 隐私与安全性 -> 辅助功能」授权；entry 脚本会探测并提示。
"""
