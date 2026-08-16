"""e2e_windows — GraphStudio Windows 安装态 E2E 测试 harness。

对外入口是 scripts/run_e2e_windows.py；本包内部分工：
  msix.py     包管理适配（信任证书 / 安装 / 查询 / 卸载 / 启动）
  app.py      AppSession（UIA 附加、真实输入、画布几何、原生对话框、WM_DROPFILES）
  fixtures.py 自包含测试夹具（graph.json + 现场生成的 PNG）
  report.py   运行目录与失败现场采集（截图 / UIA 树转储 / summary.json）
  scenarios_*.py 四组场景实现
"""
