"""plugin_cases.py — core 场景的插件用例注册表（数据驱动）。

新增一个插件子类的用例 = 往 CASES 里加一条 PluginCase，不需要写交互代码：
场景驱动器（scenarios_core.py）负责统一的「建节点 → 设参数 → 连线 → 执行 →
断言」流程。节点落点由驱动器按画布布局计算；参数按 label 在属性面板的
Parameters 分组定位。

字段说明:
  name             用例名（报告显示 core/<name>）
  nodes            按创建顺序的节点；params 为 {label: value}，label 即属性
                   面板 Parameters 分组里的行标签（如 "file_path"），value 统一
                   以文本输入（int/float 参数对应 SpinBox，string/file 对应
                   LineEdit，均可「点击→全选→粘贴→回车」赋值）
  edges            (源节点序号, 目标节点序号) 列表，按创建序号连线
  requires_gpu     True 且应用日志无 GPU 后端初始化时整例 skip
  expect_results   执行后结果下拉中应出现的 task_type 子串（None=不查下拉）
  expect_files     执行后应存在的磁盘文件（相对 report.run_dir，绝对路径开头
                   的按绝对路径查）——如 image_writer 的输出
  expect_log       执行日志中应出现的子串（None=只要求 Execution finished 且 0 failed）
"""

from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class PluginCase:
    name: str
    nodes: list[dict]                    # {"category", "type", "params": {label: value}}
    edges: list[tuple[int, int]] = field(default_factory=list)
    requires_gpu: bool = False
    expect_results: list[str] | None = None
    expect_files: list[str] = field(default_factory=list)
    expect_log: list[str] = field(default_factory=list)
    enabled: bool = True                 # False = 记 skip 不执行


READER = {"category": "Input", "type": "opencv_image_read", "params": {}}


def _reader() -> dict:
    # $ASSET 占位符：驱动器替换为夹具资产绝对路径
    return {**READER, "params": {"file_path": "$ASSET"}}


# JS 算术脚本（无图像输入，输出 float value）：验证 Scripting 分类 + 脚本内
# 声明的额外参数（a/b → 属性面板 SpinBox）。语法同 js_task 的 engine_arith.js。
JS_ARITH = """// e2e fixture: params + computed float output
const inputs = [];
const outputs = [{ name: "value", type: "float" }];
const params = [
    { name: "a", type: "float", default: 2 },
    { name: "b", type: "float", default: 21 },
];
function execute(ctx) {
    ctx.log("e2e_js_arith running");
    ctx.setOutput("value", ctx.param("a") * ctx.param("b"));
}
"""


CASES: list[PluginCase] = [
    PluginCase(
        name="input_filter",
        nodes=[_reader(),
               {"category": "OpenCV Filter", "type": "opencv_gaussian_blur_filter",
                "params": {"kernel_size": "9"}}],
        edges=[(0, 1)],
        expect_results=["opencv_gaussian_blur_filter"],
    ),
    PluginCase(
        name="filter_chain",
        nodes=[_reader(),
               {"category": "OpenCV Filter", "type": "opencv_blur_filter",
                "params": {"kernel_size": "5"}},
               {"category": "OpenCV Filter", "type": "opencv_sobel_filter",
                "params": {}}],
        edges=[(0, 1), (1, 2)],
        expect_results=["opencv_sobel_filter"],
    ),
    PluginCase(
        name="geometry_resize",
        nodes=[_reader(),
               {"category": "OpenCV Geometry", "type": "opencv_resize",
                "params": {"width": "64", "height": "48"}}],
        edges=[(0, 1)],
        expect_results=["opencv_resize"],
    ),
    PluginCase(
        name="color_cvt",
        nodes=[_reader(),
               {"category": "OpenCV Color", "type": "opencv_cvt_color", "params": {}}],
        edges=[(0, 1)],
        expect_results=["opencv_cvt_color"],
    ),
    PluginCase(
        name="color_grade_bcs",
        nodes=[_reader(),
               {"category": "Color Grading", "type": "color_grade_bcs",
                "params": {"brightness": "0.1", "saturation": "1.2"}}],
        edges=[(0, 1)],
        expect_results=["color_grade_bcs"],
    ),
    PluginCase(
        name="gpu_grayscale",
        nodes=[_reader(),
               {"category": "GPU", "type": "gpu_grayscale", "params": {}}],
        edges=[(0, 1)],
        requires_gpu=True,
        expect_results=["gpu_grayscale"],
    ),
    PluginCase(
        name="scripting_js",
        # a/b 用脚本声明的默认值：属性面板只在节点选中时按 paramSpecs 重建，
        # script_path 提交后 a/b 行不会即时出现，故不单独设置。
        # 执行成功（0 failed）即证明 execute() 运行（脚本抛错会使任务 FAILED）；
        # ctx.log 走任务 logBuffer，不保证落在 Log 面板，故无日志子串断言。
        nodes=[{"category": "Scripting", "type": "js_script",
                "params": {"script_path": "$JS"}}],
        edges=[],
        expect_log=[],
    ),
    PluginCase(
        name="output_write",
        nodes=[_reader(),
               {"category": "OpenCV Filter", "type": "opencv_gaussian_blur_filter",
                "params": {}},
               {"category": "Output", "type": "opencv_image_write",
                "params": {"file_path": "$OUT/write_out.png"}}],
        edges=[(0, 1), (1, 2)],
        expect_files=["$OUT/write_out.png"],
    ),
]
