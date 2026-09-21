# -*- coding: utf-8 -*-
"""节点手册中文元数据。键 = task type（与 task_specs.json / URL slug 一致）。

字段：title（页面标题）、summary（列表/og 摘要）、desc（概述段，支持多行）、
notes（注意事项列表）、params（任务级参数说明，优先于 COMMON_PARAMS 与
ParamSpec.description）。参数表本身由 task_specs.json 生成，这里只补语义。
"""

MODULES = {
    "core": "核心库",
    "image_reader": "图像读取",
    "image_writer": "图像写出",
    "image_filtering": "OpenCV 图像滤波",
    "image_geometry": "OpenCV 几何变换",
    "image_color": "OpenCV 色彩与二值化",
    "image_color_grading": "调色（Color Grading）",
    "image_enhance": "OpenCV 图像增强",
    "image_segmentation": "OpenCV 图像分割",
    "video_io": "视频读写",
    "gpu_image_processing": "GPU 图像处理",
    "render_task": "GPU 渲染",
    "blend": "图层混合",
    "face_detect": "人脸检测",
    "matting": "人像抠像",
}

COMMON_PARAMS = {
    "kernel_size": "核边长（像素），须为正奇数",
    "ksize": "核边长（像素），须为正奇数",
    "sigma": "高斯标准差；0 表示按核尺寸自动推算",
    "sigma_x": "水平方向标准差；0 表示自动推算",
    "sigma_y": "垂直方向标准差；0 时与水平方向一致",
    "sigma_color": "色彩空间滤波强度（值大 = 同等颜色的更大范围被平滑）",
    "sigma_space": "坐标空间滤波强度（值大 = 更远像素互相影响）",
    "dx": "x 方向求导阶数",
    "dy": "y 方向求导阶数",
    "iterations": "重复执行次数",
    "anchor_x": "核锚点 x；-1 表示核中心",
    "anchor_y": "核锚点 y；-1 表示核中心",
    "normalize": "是否按核面积归一化",
    "border_type": "边界像素填充模式",
    "interpolation": "插值方法",
    "depth": "输出位深（-1 表示与输入一致）",
    "backend": "推理后端选择；Auto 按可用性自动降级（MediaPipe → MNN）",
    "delegate": "MediaPipe 后端的推理委托（CPU / GPU）",
    "device": "MNN 后端设备（CPU / Metal / Auto）",
    "threads": "CPU 推理线程数",
    "width": "目标宽度（像素）；0 表示跟随输入尺寸",
    "height": "目标高度（像素）；0 表示跟随输入尺寸",
    "script_path": "自定义渲染脚本文件路径（效果清单引用；留空用内置效果）",
    "format": "渲染目标纹理格式",
    "clear": "pass 开始时是否清屏",
    "clear_color": "清屏颜色 RGBA，逗号分隔四个 0-255/0-1 分量",
    "blend": "是否启用混合输出",
    "opacity": "不透明度；1 为完全不透明，0 为完全透明",
}

META = {
    # ---------- 核心库 ----------
    "io_input": dict(
        title="io_input · 图边界输入",
        summary="SDK 嵌入式执行的图边界：宿主在执行入口把绑定值写入 io_input，图内任务从其 out 端口消费。",
        desc="图边界输入节点，供嵌入式宿主（SDK）注入数据：执行前宿主把绑定的图像/张量写入本节点，"
             "图内其余任务从它的 `out` 端口读取。在 GraphStudio 画布上一般不需要手动创建——"
             "编辑器场景请使用 `opencv_image_read` 等源节点。",
        notes=["任务类型由核心库直接注册，不依赖任何子模块。"],
    ),
    "io_output": dict(
        title="io_output · 图边界输出",
        summary="SDK 嵌入式执行的图边界：执行结束后宿主从 io_output 收集图计算结果。",
        desc="图边界输出节点：执行结束后宿主从它收集计算结果（上游端口的值原样透传）。"
             "GraphStudio 画布上一般不需要手动创建，结果查看请用图像查看器或 `opencv_image_write`。",
        notes=["任务类型由核心库直接注册，不依赖任何子模块。"],
    ),

    # ---------- 图像读取 / 写出 ----------
    "opencv_image_read": dict(
        title="opencv_image_read · 读取图像",
        summary="从磁盘读取一张图像（png / jpg / bmp / tiff / webp 等 OpenCV 支持的格式），作为图的输入源。",
        desc="读取一张图像并从 `out` 端口输出，是最常用的图输入源。支持 OpenCV codec 覆盖的全部常见格式。"
             "默认输出 BGR 三通道；开启 `keep_alpha` 后保留 alpha 通道（四通道 RGBA），"
             "供混合、抠像等需要透明度的下游任务使用。",
        params={
            "file_path": "图像文件路径。相对路径按图 JSON 所在目录探测（图目录及其上级），"
                         "绝对路径原样使用",
        },
        notes=["路径缺失或解码失败时任务返回 FAILED，日志面板会给出可读原因。"],
    ),
    "opencv_image_write": dict(
        title="opencv_image_write · 写出图像",
        summary="把上游图像写出为文件，格式由扩展名推断（.png / .jpg / .bmp …）。",
        desc="把输入端口收到的图像写到 `file_path`。输出格式由文件扩展名决定，"
             "透明通道会随 PNG 等支持 alpha 的格式保留。",
        params={
            "file_path": "输出图像路径。相对路径按图 JSON 所在目录拼接（不做资产探测，"
                         "避免覆盖原始资产）",
        },
    ),

    # ---------- OpenCV 图像滤波（17） ----------
    "opencv_blur_filter": dict(
        title="opencv_blur_filter · 均值模糊",
        summary="方框均值模糊：核窗口内像素取平均，最快的平滑手段。",
        desc="标准均值模糊（`cv::blur`）。核越大越平滑，细节与噪声一起被抹掉；"
             "常作为下采样前的预滤波或轻度去噪使用。",
    ),
    "opencv_gaussian_blur_filter": dict(
        title="opencv_gaussian_blur_filter · 高斯模糊",
        summary="高斯加权模糊：核内按正态分布加权平均，平滑更自然，是去噪/背景虚化的默认选择。",
        desc="高斯模糊（`cv::GaussianBlur`）。`sigma` 为 0 时按核尺寸自动推算标准差。"
             "与 GPU 渲染侧的 `render_gauss_h` / `render_gauss_v` 语义对应，后者在 GPU 上可分离执行。",
    ),
    "opencv_median_blur_filter": dict(
        title="opencv_median_blur_filter · 中值模糊",
        summary="中值滤波：核内取中位数，对椒盐噪声极其有效且保边。",
        desc="中值滤波（`cv::medianBlur`）。对脉冲式噪声（椒盐点）效果远好于线性滤波，"
             "且不会像均值那样糊掉边缘，代价是计算量更高。",
    ),
    "opencv_bilateral_filter": dict(
        title="opencv_bilateral_filter · 双边滤波",
        summary="保边去噪：同时按空间距离与色彩差异加权，磨皮/去噪的同时保留轮廓。",
        desc="双边滤波（`cv::bilateralFilter`）。`sigma_color` 控制多大色差被视为『同一颜色』，"
             "`sigma_space` 控制空间影响半径——两者一起决定平滑强度。"
             "人像磨皮、卡通化预处理的常用一步。",
    ),
    "opencv_box_filter": dict(
        title="opencv_box_filter · 方框滤波",
        summary="方框滤波：核内求和（可归一化为均值），是均值模糊的通用形式。",
        desc="方框滤波（`cv::boxFilter`）。`normalize` 开启时等价均值模糊；关闭时输出核内像素之和"
             "（用于积分类计算的中间形态）。",
    ),
    "opencv_sobel_filter": dict(
        title="opencv_sobel_filter · Sobel 导数",
        summary="Sobel 算子求图像梯度（dx / dy 阶数可选），边缘检测的基本积木。",
        desc="Sobel 导数（`cv::Sobel`）。`dx`/`dy` 指定求导方向与阶数（和须小于核尺寸）；"
             "输出幅度图，常接 `opencv_threshold` 二值化得到边缘。",
    ),
    "opencv_scharr_filter": dict(
        title="opencv_scharr_filter · Scharr 导数",
        summary="Scharr 算子：3× 核下比 Sobel 更精确的旋转对称导数，小核边缘检测首选。",
        desc="Scharr 导数（`cv::Scharr`）。仅 3×3 核，但角度误差小于同尺寸 Sobel；"
             "需要更高精度梯度或 HK 轮廓提取时用它替换 Sobel。",
    ),
    "opencv_laplacian_filter": dict(
        title="opencv_laplacian_filter · 拉普拉斯算子",
        summary="二阶导数算子：各向同性的边缘/纹理响应，锐化与边缘检测两用。",
        desc="拉普拉斯算子（`cv::Laplacian`）。对噪声敏感，通常先做高斯平滑再取拉普拉斯（LoG 思路）。"
             "ksize=1 时为经典四邻域核。",
    ),
    "opencv_filter_2d": dict(
        title="opencv_filter_2d · 自定义卷积",
        summary="用任意自定义核做 2D 卷积/相关：把你的核写成数值矩阵即可。",
        desc="通用 2D 卷积（`cv::filter2D`）。核以字符串形式给出（行内逗号、行间分号，如 "
             "`0,-1,0;-1,5,-1;0,-1,0`），适合试验自定义算子而不写代码。",
        params={"kernel": "卷积核内容：行间用分号、行内用逗号分隔的数值矩阵"},
    ),
    "opencv_sep_filter_2d": dict(
        title="opencv_sep_filter_2d · 可分离卷积",
        summary="两个一维核先后卷积（先横后纵），大核时的提速版 filter2d。",
        desc="可分离卷积（`cv::sepFilter2D`）。把二维核分解为行、列两个一维核分别传入，"
             "计算量从 k² 降到 2k——大核高斯类滤波的正确姿势。",
        params={
            "kernel_x": "水平方向一维核（逗号分隔数值）",
            "kernel_y": "垂直方向一维核（逗号分隔数值）",
        },
    ),
    "opencv_sqr_box_filter": dict(
        title="opencv_sqr_box_filter · 平方方框滤波",
        summary="核内像素平方的均值（归一化），局部能量/方差估计的积木。",
        desc="平方方框滤波（`cv::sqrBoxFilter`）。输出的每点是核内像素平方的（归一化）和，"
             "配合 `opencv_box_filter` 可推出局部方差，用于纹理/噪声强度分析。",
    ),
    "opencv_gabor_filter": dict(
        title="opencv_gabor_filter · Gabor 滤波",
        summary="Gabor 核方向纹理滤波：指纹/织物纹理与方向性特征提取的经典手段。",
        desc="Gabor 滤波（`cv::getGaborKernel` + filter2D）。按波长、方向、带宽生成正弦调制高斯核，"
             "对特定方向与频率的纹理响应最强；多组参数并联可做纹理特征。",
        params={
            "ksize_x": "核宽（像素）", "ksize_y": "核高（像素）",
            "sigma": "高斯包络标准差",
            "theta": "滤波方向角（弧度）",
            "lambd": "正弦波长（像素）",
            "gamma": "空间纵横比",
            "psi": "相位偏移（弧度）",
        },
    ),
    "opencv_dilate": dict(
        title="opencv_dilate · 膨胀",
        summary="形态学膨胀：核内取最大值，亮区扩张、连接断裂。",
        desc="形态学膨胀（`cv::dilate`）。白色区域按核形状向外扩张，用于桥接断裂笔画、"
             "增强亮目标；与 `opencv_erode` 互为反操作。",
    ),
    "opencv_erode": dict(
        title="opencv_erode · 腐蚀",
        summary="形态学腐蚀：核内取最小值，亮区收缩、去白点噪声。",
        desc="形态学腐蚀（`cv::erode`）。白色区域按核收缩，可去掉小的亮点噪声、分离粘连物体；"
             "与 `opencv_dilate` 互为反操作。",
    ),
    "opencv_morphology_ex": dict(
        title="opencv_morphology_ex · 形态学组合操作",
        summary="开/闭/梯度/顶帽/黑帽等形态学组合：一次节点完成膨胀腐蚀的组合拳。",
        desc="组合形态学（`cv::morphologyEx`）：开运算去小白点、闭运算填小黑洞、"
             "梯度取形态学边缘、顶帽/黑帽提取比邻域亮/暗的局部结构。"
             "GPU 渲染侧可用 `render_dilate_dir` + `render_erode_dir` 的 pass 编排表达同类计算。",
    ),
    "opencv_pyr_down": dict(
        title="opencv_pyr_down · 金字塔降采样",
        summary="高斯平滑 + 尺寸减半：抗锯齿的下采样。",
        desc="金字塔降采样（`cv::pyrDown`）。先高斯平滑再隔行隔列抽取，尺寸减半；"
             "比直接 resize 更不容易出现摩尔纹。",
    ),
    "opencv_pyr_up": dict(
        title="opencv_pyr_up · 金字塔上采样",
        summary="插值放大一倍 + 高斯模糊：金字塔逆操作。",
        desc="金字塔上采样（`cv::pyrUp`）。尺寸翻倍后做高斯模糊，与 `opencv_pyr_down` 互为逆过程"
             "（往返会丢失高频信息）。",
    ),

    # ---------- OpenCV 几何变换（5） ----------
    "opencv_resize": dict(
        title="opencv_resize · 缩放",
        summary="按目标宽高与插值方法缩放图像。",
        desc="图像缩放（`cv::resize`）。缩小用 `INTER_AREA` 抗摩尔纹、放大用 `INTER_LINEAR`/"
             "`INTER_CUBIC` 平滑；近邻 `INTER_NEAREST` 保像素值（掩码/标签图必用）。",
    ),
    "opencv_flip": dict(
        title="opencv_flip · 翻转",
        summary="水平 / 垂直 / 双向翻转（flip code 0 / 1 / -1）。",
        desc="镜像翻转（`cv::flip`）。`flip_code` 为 1 水平镜像、0 垂直镜像、-1 双向（旋转 180°）。"
             "批量数据增强或摄像头自拍镜像常用。",
        params={"flip_code": "翻转方向：1 = 水平，0 = 垂直，-1 = 双向"},
    ),
    "opencv_rotate": dict(
        title="opencv_rotate · 直角旋转",
        summary="90° / 180° / 270° 快速旋转（rotate code 0/1/2）。",
        desc="直角旋转（`cv::rotate`）。仅支持 90° 的整数倍，无插值损失；任意角度请用 "
             "`opencv_warp_affine`。",
        params={"rotate_code": "旋转量：0 = 90° 顺时针，1 = 180°，2 = 270° 顺时针"},
    ),
    "opencv_warp_affine": dict(
        title="opencv_warp_affine · 仿射变换",
        summary="按 2×3 仿射矩阵做平移/旋转/缩放/错切，任意角度旋转走这里。",
        desc="仿射变换（`cv::warpAffine`）。变换矩阵以 2×3 六元数字符串给出（行主序，如平移 "
             "`1,0,20;0,1,10`）；保持平行关系的变换都能表达。",
        params={"matrix": "2×3 仿射矩阵：行间分号、行内逗号分隔的六个数"},
    ),
    "opencv_transpose": dict(
        title="opencv_transpose · 转置",
        summary="行列互换（沿主对角线翻转）。",
        desc="矩阵转置（`cv::transpose`）。宽高互换，等效沿主对角线镜像。",
    ),

    # ---------- OpenCV 色彩与二值化（4） ----------
    "opencv_cvt_color": dict(
        title="opencv_cvt_color · 色彩空间转换",
        summary="BGR/GRAY/RGB/HSV/HLS/LAB/LUV/YCrCb/XYZ 之间的常用转换。",
        desc="色彩空间转换（`cv::cvtColor`）。按 `code` 枚举选择转换方向。"
             "去 `opencv_threshold` 前先转 HSV 可以按亮度/饱和度分割，是调色和掩膜流程的常驻节点。",
        notes=["8 位图转 HSV 时 H 范围是 0-179（OpenCV 约定），不是 0-359。"],
    ),
    "opencv_threshold": dict(
        title="opencv_threshold · 固定阈值二值化",
        summary="按阈值二值化/截断/归零，含 OTSU 与 TRIANGLE 自动阈值。",
        desc="固定阈值（`cv::threshold`）。`type` 选择二值/反二值/截断/归零语义；"
             "`OTSU_BINARY` / `TRIANGLE_BINARY` 忽略 `thresh` 自动推算全局阈值。",
        notes=["OTSU / TRIANGLE 模式下 `thresh` 参数被忽略；输入须为 8 位单通道（先转灰度）。"],
    ),
    "opencv_adaptive_threshold": dict(
        title="opencv_adaptive_threshold · 自适应阈值",
        summary="按邻域局部统计（均值/高斯）二值化，抗光照不均。",
        desc="自适应阈值（`cv::adaptiveThreshold`）。阈值逐像素由邻域均值/高斯加权减去常数 C 得到，"
             "适合光照渐变的文档扫描、阴影场景。",
        params={
            "block_size": "邻域边长（正奇数）",
            "c": "从局部均值中减去的常数；越大越严格",
        },
    ),
    "opencv_apply_color_map": dict(
        title="opencv_apply_color_map · 伪彩色映射",
        summary="把单通道图按预置色表上色（JET / VIRIDIS / BONE …），热力图可视化一步到位。",
        desc="伪彩色映射（`cv::applyColorMap`）。灰度/深度/置信度图按 `colormap` 枚举染色，"
             "输出三通道可视化图。",
    ),

    # ---------- 调色 Color Grading（6） ----------
    "color_grade_wheels": dict(
        title="color_grade_wheels · 色轮调色",
        summary="Lift / Gamma / Gain 三色轮：阴影、中间调、高光分别做 RGB 偏移与整体明度调整。",
        desc="按 DaVinci 风格三色轮调色：`lift`（阴影）、`gamma`（中间调）、`gain`（高光）各自由 "
             "RGB 偏移 + 明度构成。字符串按 RGB + 亮度四分量给出，负值偏青、正值偏暖"
             "（数值含义为乘性/加性组合，见参数表默认形态）。",
        params={
            "lift": "阴影轮：R,G,B 偏移 + 亮度，逗号分隔",
            "gamma": "中间调轮：R,G,B 偏移 + 亮度",
            "gain": "高光轮：R,G,B 偏移 + 亮度",
        },
    ),
    "color_grade_curves": dict(
        title="color_grade_curves · 曲线调色",
        summary="主曲线与 R/G/B 通道曲线：控制点字符串描述的样条调色。",
        desc="Photoshop 曲线式调色。每条曲线用控制点序列（x,y 对，逗号分隔）描述，"
             "按单调样条插值成 256 级查找表后应用。主曲线调明度对比，RGB 曲线调色偏。",
        params={
            "master": "主曲线控制点：x1,y1,x2,y2,…",
            "r": "红色通道曲线控制点", "g": "绿色通道曲线控制点", "b": "蓝色通道曲线控制点",
        },
    ),
    "color_grade_lut": dict(
        title="color_grade_lut · LUT 调色",
        summary="应用 .cube 等 3D LUT 文件做调色（三线性 / 四面体插值）。",
        desc="读 3D LUT 文件（`.cube`）应用到图像。`interpolation` 选三线性（快）或四面体"
             "（更平滑）。摄影调色包通常直接是 .cube 文件，拖进来即用。",
        params={
            "lut_file": "LUT 文件路径（.cube）；相对路径按图目录探测",
            "interpolation": "插值方式：TRILINEAR 三线性 / TETRAHEDRAL 四面体",
        },
        notes=["GPU 渲染侧等价物是 `render_lut_cube`（.cube → HALD 图）+ `render_lut`（应用），"
               "大数据量时更快。"],
    ),
    "color_grade_bcs": dict(
        title="color_grade_bcs · 亮度/对比度/饱和度",
        summary="一次节点同时调亮度、对比度、饱和度。",
        desc="经典 BCS 三连调：`brightness` 加减明度、`contrast` 拉开层次、`saturation` 调色彩浓度。"
             "所有参数以 0 为中性（或 1 为乘性中性，见默认值）。",
    ),
    "color_grade_mixer": dict(
        title="color_grade_mixer · 通道混合器",
        summary="RGB 输出通道按输入通道加权重组，做色调分离/通道对调。",
        desc="通道混合器：每个输出通道 = R/G/B 输入的加权和（3×3 权重字符串）。"
             "经典玩法包括红青对调、黑白转换权重微调、伪红外色调。",
        params={"matrix": "3×3 通道权重：行间分号、行内逗号（out_R = 第一行）"},
    ),
    "color_grade_hsl": dict(
        title="color_grade_hsl · HSL 八通道调色",
        summary="按色相分区（红/黄/绿/青/蓝/洋红 + 明度/饱和度）精细调色。",
        desc="HSL 分区调色：先选色相区间，再分别调该区间的色相偏移、饱和度与明度；"
             "另附全图饱和度/明度。字符串按 6 个色相分区 × 3 分量给出。",
        params={"adjustments": "六分区调整量：每区 hue,sat,lum 三元组"},
    ),

    # ---------- OpenCV 图像增强（8） ----------
    "opencv_equalize_hist": dict(
        title="opencv_equalize_hist · 直方图均衡",
        summary="全局直方图均衡化：拉开动态范围，灰图立刻通透。",
        desc="直方图均衡（`cv::equalizeHist`）。把 8 位单通道图的灰度分布拉平，"
             "低对比度照片/医学影像的快速增强。彩色图请先分通道或转 YUV 处理亮度。",
    ),
    "opencv_clahe": dict(
        title="opencv_clahe · CLAHE 限制对比度均衡",
        summary="分块自适应直方图均衡：局部增强且噪声放大可控（clip limit + 网格）。",
        desc="限制对比度自适应直方图均衡（`cv::createCLAHE`）。按 `tile` 网格分块均衡、"
             "`clip_limit` 限制每格直方图高度防止噪声放大；医学/夜景增强的主力。",
        params={
            "clip_limit": "对比度限制强度（1-4 常用）",
            "tile_grid_size": "分块网格（如 8x8，写法见默认值）",
        },
    ),
    "opencv_sharpen": dict(
        title="opencv_sharpen · 锐化",
        summary="经典反锐化掩模锐化（unsharp mask 一站式封装）。",
        desc="锐化滤波：原图 + amount ×（原图 - 模糊图）的经典 USM。`amount` 控制强度，"
             "过度锐化会放大噪声与光晕。",
        params={"amount": "锐化强度"},
    ),
    "opencv_denoise": dict(
        title="opencv_denoise · 非局部均值去噪",
        summary="fastNlMeansDenoising：按图像块相似度去噪，细节保留最好。",
        desc="非局部均值去噪（`cv::fastNlMeansDenoising`）。以相似块聚合替代局部平滑，"
             "高斯噪声去除效果与细节保留都优于双边滤波，代价是速度较慢。`h` 越大去得越狠。",
        params={"h": "滤波强度（亮度）：10 左右起步"},
    ),
    "opencv_add_weighted": dict(
        title="opencv_add_weighted · 加权融合",
        summary="两路输入按 alpha/beta/gamma 线性混合：叠图、淡入淡出一节点搞定。",
        desc="线性混合（`cv::addWeighted`）：`out = alpha·in + beta·in2 + gamma`。"
             "双输入端口，常用于曝光合成、水印叠加、动画过渡。",
        params={
            "alpha": "第一输入权重", "beta": "第二输入权重",
            "gamma": "加性偏移",
        },
        notes=["两路输入尺寸与通道数需一致。"],
    ),
    "opencv_gamma_correct": dict(
        title="opencv_gamma_correct · 伽马校正",
        summary="按幂律（out = in^gamma）调整明暗，显示器匹配与提亮暗部。",
        desc="伽马校正：查表实现幂律变换。`gamma` < 1 提亮暗部、> 1 压暗；"
             "线性→sRGB 或逆向转换也是同一节点。",
        params={"gamma": "幂指数；0.45 附近为线性→sRGB"},
    ),
    "opencv_brightness_contrast": dict(
        title="opencv_brightness_contrast · 亮度对比度",
        summary="按 gain / bias 线性调整亮度与对比度。",
        desc="线性明暗调整：`out = in * contrast + brightness`。最常用的观感微调节点，"
             "调色链路里通常放在 BCS 之前做基础校正。",
    ),
    "opencv_invert": dict(
        title="opencv_invert · 反色",
        summary="像素取反（255 - x），负片效果 / 掩膜取反。",
        desc="反色（`cv::bitwise_not` 语义）。生成负片、反转掩膜（前景↔背景）都是一步完成。",
    ),

    # ---------- OpenCV 图像分割（5） ----------
    "opencv_grabcut": dict(
        title="opencv_grabcut · GrabCut 前背景分割",
        summary="以矩形或掩膜初始化的迭代图割分割：交互式抠图的经典算法。",
        desc="GrabCut（`cv::grabCut`）。`init_mode` 选矩形（给定包含目标的框）或掩膜"
             "（给定粗略前后景标记），迭代图割能量最小化输出精细前景掩膜。"
             "人像/物体抠图的 OpenCV 侧入口；高质量人像请用 `matting` 节点。",
        notes=["RECT 模式下 rect_x/y/width/height 定义目标框；MASK 模式忽略矩形参数。",
               "迭代越多边缘越精细也越慢，5-10 次通常够用。"],
    ),
    "opencv_watershed": dict(
        title="opencv_watershed · 分水岭分割",
        summary="经典分水岭：配合标记图（marker）分割粘连目标。",
        desc="分水岭算法（`cv::watershed`）。需要输入图像 + 标记图（不同整数标记不同种子区域，"
             "0 为未知），按梯度地形淹没求得分割边界。"
             "上游常用 `opencv_threshold` + `opencv_connected_components` 生成标记。",
    ),
    "opencv_flood_fill": dict(
        title="opencv_flood_fill · 泛洪填充",
        summary="从种子点按相似度泛洪：选区填充 / 魔棒效果。",
        desc="泛洪填充（`cv::floodFill`）。从 (seed_x, seed_y) 出发，把与种子颜色差异在"
             "lo_diff/up_diff 范围内的连通区域替换为指定颜色，返回填充掩膜。",
        params={
            "seed_x": "种子点 x 坐标", "seed_y": "种子点 y 坐标",
            "lo_diff": "向下容差", "up_diff": "向上容差",
            "new_value": "填充颜色",
        },
    ),
    "opencv_connected_components": dict(
        title="opencv_connected_components · 连通域标记",
        summary="二值图连通域逐个编号，输出标签图（计数见日志）。",
        desc="连通域分析（`cv::connectedComponents`）。8/4 连通可选，输出每个像素所属域的"
             "整数标签；配合统计可做计数、去小面积噪声。",
        params={"connectivity": "连通性：8 或 4"},
    ),
    "opencv_distance_transform": dict(
        title="opencv_distance_transform · 距离变换",
        summary="前景像素到最近背景的距离场：骨架化/宽度估计的中间量。",
        desc="距离变换（`cv::distanceTransform`）。非零像素到最近零像素的距离，"
             "距离类型与核尺寸可选；分水岭种子提取、笔画宽度估计的经典前置步骤。",
    ),

    # ---------- 视频读写（2） ----------
    "video_reader": dict(
        title="video_reader · 读取视频",
        summary="逐帧读取视频文件（mp4 / mov / avi …），每次执行输出一帧。",
        desc="视频读取节点：executor 按帧驱动，每次 `execute()` 从 `out` 输出下一帧，"
             "流结束后收尾。`api_preference` 强制选择解码后端（默认自动，FFMPEG 最常用）。",
        params={
            "file_path": "视频文件路径；相对路径按图目录探测",
            "api_preference": "解码后端：DEFAULT 自动 / FFMPEG",
        },
        notes=["逐帧语义由图的执行器驱动，GraphStudio 单次执行等于处理一帧；"
               "批量转码用 SDK 嵌入式执行或脚本循环。"],
    ),
    "video_writer": dict(
        title="video_writer · 写出视频",
        summary="把帧序列编码写出为视频文件（fourcc / fps / 是否彩色可配）。",
        desc="视频写出节点：上游每帧写入，图执行收尾时关闭文件并写 trailer。"
             "`fourcc` 用四字符编码名（如 `mp4v`），`fps` 必须与读取端一致。",
        params={
            "file_path": "输出视频路径（扩展名决定容器）",
            "fourcc": "四字符编码标识，如 mp4v / avc1",
            "fps": "帧率；须与源一致否则快慢放",
            "is_color": "是否写彩色帧",
        },
    ),

    # ---------- GPU 图像处理（17） ----------
    "gpu_box_blur": dict(
        title="gpu_box_blur · GPU 均值模糊",
        summary="GPU compute 管线的方框均值模糊，与 opencv_blur_filter 语义对齐。",
        desc="在 GPU compute 后端（wgpu/Metal/Vulkan）上执行的均值模糊，CPU 参考实现逐位对齐。"
             "大图批量处理时比 CPU 版快一个量级，参数语义与 `opencv_blur_filter` 一致。",
        notes=["GPU 后端初始化失败时任务失败（不会静默回退 CPU；需要回退语义请用 blend 的 device=auto）。"],
    ),
    "gpu_gaussian_blur": dict(
        title="gpu_gaussian_blur · GPU 高斯模糊",
        summary="GPU compute 高斯模糊，kernel 为 WGSL/MSL/GLSL 单源内置。",
        desc="GPU 上的高斯模糊，语义对齐 `opencv_gaussian_blur_filter`。"
             "作为 GPU 链路的通用预处理节点，输出保持 GPU 驻留，可继续接其他 gpu_* 节点零拷贝。",
    ),
    "gpu_grayscale": dict(
        title="gpu_grayscale · GPU 灰度化",
        summary="GPU 上的 RGB→灰度（BGR2GRAY 定点系数），无参数。",
        desc="GPU 灰度化：按 OpenCV BGR2GRAY 的定点系数（4899R+9617G+1868B）计算，"
             "与 CPU 结果逐位一致（unorm 精度内）。",
    ),
    "gpu_brightness_contrast": dict(
        title="gpu_brightness_contrast · GPU 亮度对比度",
        summary="GPU 上的线性亮度/对比度调整。",
        desc="GPU 版线性明暗调整，语义同 `opencv_brightness_contrast`。实时预览链路的常驻节点。",
    ),
    "gpu_resize": dict(
        title="gpu_resize · GPU 缩放",
        summary="GPU 上的双线性缩放。",
        desc="GPU 双线性缩放。需要最近邻等其它插值语义时请用 CPU 版 `opencv_resize`。",
    ),
    "gpu_threshold": dict(
        title="gpu_threshold · GPU 阈值二值化",
        summary="GPU 上的固定阈值二值化。",
        desc="GPU 版固定阈值二值化，语义同 `opencv_threshold` 的 BINARY 模式；"
             "OTSU 自动阈值请用 CPU 版。",
    ),
    "gpu_gamma": dict(
        title="gpu_gamma · GPU 伽马校正",
        summary="GPU 上的幂律伽马校正。",
        desc="GPU 伽马查表（GPU 上直接幂计算），语义同 `opencv_gamma_correct`。",
    ),
    "gpu_invert": dict(
        title="gpu_invert · GPU 反色",
        summary="GPU 像素取反，无参数。",
        desc="GPU 版反色（1 - x 归一化坐标），语义同 `opencv_invert`。",
    ),
    "gpu_rgb2hsv": dict(
        title="gpu_rgb2hsv · GPU RGB→HSV",
        summary="GPU 上的 RGB→HSV 色彩空间转换，无参数。",
        desc="GPU 版 RGB→HSV 转换。注意 GPU 输出的 H 通道归一化到 [0,1]"
             "（CPU OpenCV 8 位图为 0-179），下游比较阈值要相应缩放。",
    ),
    "gpu_flip": dict(
        title="gpu_flip · GPU 翻转",
        summary="GPU 上的图像翻转（水平/垂直/双向）。",
        desc="GPU 翻转：在 shader 里按 flip code 交换坐标，语义同 `opencv_flip`。",
    ),
    "gpu_rotate90": dict(
        title="gpu_rotate90 · GPU 直角旋转",
        summary="GPU 上的 90° 整倍数旋转。",
        desc="GPU 直角旋转：坐标重映射实现（无插值损失），语义同 `opencv_rotate`。",
    ),
    "gpu_crop": dict(
        title="gpu_crop · GPU 裁剪",
        summary="GPU 上的矩形裁剪（x/y/宽高）。",
        desc="GPU 裁剪：按 (x, y, width, height) 矩形截取子图，输出尺寸随之变化。",
    ),
    "gpu_sharpen": dict(
        title="gpu_sharpen · GPU 锐化",
        summary="GPU 上的反锐化掩模锐化。",
        desc="GPU 锐化，语义同 `opencv_sharpen`；`amount` 控制强度。",
    ),
    "gpu_sobel": dict(
        title="gpu_sobel · GPU Sobel 导数",
        summary="GPU 上的 Sobel 梯度（幅度图输出）。",
        desc="GPU Sobel：幅度语义对齐 `convertScaleAbs`（|gx|/|gy| 各自饱和后 0.5 加权）。"
             "与 CPU 版的核系数标定一致，内边距误差 ≤1/255。",
    ),
    "gpu_laplacian": dict(
        title="gpu_laplacian · GPU 拉普拉斯算子",
        summary="GPU 上的拉普拉斯二阶导数，无参数。",
        desc="GPU 拉普拉斯（四邻域核），语义对齐 `opencv_laplacian_filter` 的 ksize=1。",
    ),
    "gpu_blend": dict(
        title="gpu_blend · GPU 双图混合",
        summary="GPU 上的两路输入线性混合（opacity 插值）。",
        desc="GPU 双输入线性混合：`out = mix(in, in2, opacity)`。"
             "注意它与 `blend` 任务（Photoshop 27 种混合模式）不是同一节点——"
             "后者是独立子模块、模式更丰富且带 CPU 回退。",
        params={"opacity": "in2 的混合权重"},
    ),
    "gpu_alpha_composite": dict(
        title="gpu_alpha_composite · GPU Alpha 合成",
        summary="GPU 上的 alpha 通道合成（over 操作），前景 in 压到背景 in2 上。",
        desc="GPU alpha 合成：`in`（带 alpha 前景）over `in2`（背景）。"
             "matting/抠像输出的 RGBA 直通到本节点合成最终画面。",
        notes=["`in2` 须为三通道（BGR）；两路输入尺寸必须一致。",
               "上游四通道请用 `opencv_image_read` 的 keep_alpha 或 matting 的 cutout 输出。"],
    ),

    # ---------- GPU 渲染（19） ----------
    "render_pass": dict(
        title="render_pass · 渲染 pass 积木",
        summary="单 pass 离屏渲染的基础积木：一个脚本 + 一次全屏三角形绘制。",
        desc="图级渲染编排的最小单元：一个 pass = 加载效果脚本 → 清屏（可选）→ 全屏三角形绘制一次。"
             "多 pass 复合请用 `render_pipeline`；本节点适合单效果调试与快速预览。",
        notes=["GPU 纹理驻留输出，可直接接 compute/render 节点继续 GPU 处理。"],
        params={"script_path": "渲染脚本路径前缀（.metal/.vert+.frag/.wgsl）；留空用内置 passthrough"},
    ),
    "render_pipeline": dict(
        title="render_pipeline · 多 pass 渲染管线",
        summary="任务级多 pass 编排：passes 数量 + pass{i}_* 扁平参数键表达整条滤镜链。",
        desc="在一个任务里串起 N 个渲染 pass：`passes` 声明数量，每个 pass 的效果与参数用 "
             "`pass1_effect`、`pass1_intensity` … 的扁平键描述（TaskParams 是扁平 map，"
             "嵌套结构装不进去）。复合滤镜（二维高斯 = 横向+纵向、形态学开闭 = 腐蚀+膨胀）"
             "都用它表达，不再新增复合任务类型。",
        params={
            "passes": "pass 数量（1-64）",
            "width": "输出画布宽；0 跟随输入", "height": "输出画布高；0 跟随输入",
            "effects_path": "效果清单（目录）路径；内置效果留空",
        },
        notes=["pass{i}_effect 指定每段效果名，其余 pass{i}_<参数> 透传给该效果。",
               "示例与可运行的范本图见仓库 `submodules/render/render_task/tests/graphs/`。"],
    ),
    "render_gradient": dict(
        title="render_gradient · 渐变生成",
        summary="无输入生成线性渐变图：测试/背景/合成底图。",
        desc="渲染一张线性渐变（颜色与方向参数化），零输入节点。"
             "常作为 render 链的信号源调试 shader，或做合成底图。",
    ),
    "render_passthrough": dict(
        title="render_passthrough · 直通",
        summary="输入原样输出：走一遍 GPU 上传/下载路径的对照节点。",
        desc="恒等渲染：图像上纹理再下载，用于验证渲染通路与格式转换；"
             "调试渲染链时作为『什么都不做』的对照。",
    ),
    "render_mix": dict(
        title="render_mix · 渲染混合",
        summary="双输入 GPU 混合（factor 插值）。",
        desc="GPU render 侧的双输入混合：按混合系数在 `in` 与 `in2` 之间插值，"
             "与 `gpu_blend` 语义相近但走渲染管线（可用自定义脚本扩展）。",
    ),
    "render_gauss_h": dict(
        title="render_gauss_h · 水平高斯（5-tap）",
        summary="内置 5-tap [1,4,6,4,1]/16 可分离高斯的水平 pass。",
        desc="固定 5-tap 二项式高斯的水平方向 pass。与 `render_gauss_v` 串联构成二维高斯"
             "（等价 `render_pipeline` 的 gauss_dir × 2）。",
    ),
    "render_gauss_v": dict(
        title="render_gauss_v · 垂直高斯（5-tap）",
        summary="内置 5-tap [1,4,6,4,1]/16 可分离高斯的垂直 pass。",
        desc="固定 5-tap 二项式高斯的垂直方向 pass，与 `render_gauss_h` 配对使用。",
    ),
    "render_threshold": dict(
        title="render_threshold · 渲染阈值",
        summary="GPU render 侧的亮度阈值二值化。",
        desc="渲染管线的阈值化：亮度低于阈值的像素置 0。与 compute 侧 `gpu_threshold` 语义一致，"
             "但输出纹理驻留、可继续接渲染效果。",
    ),
    "render_lut": dict(
        title="render_lut · GPU LUT 应用",
        summary="双输入 LUT 应用：in = 图像、in2 = LUT 图，HALD 与 stripe 布局自动识别。",
        desc="GPU 上应用 LUT 图（第二输入）：`layout` 自动识别 HALD N²×N² 与 stripe N²×N 两种"
             "布局，8 角纹素中心采样手动三线性，`intensity` 控制混合强度。"
             "LUT 图由 `render_lut_cube` 从 .cube 文件生成，或直接用任何 HALD 色卡资源。",
        params={
            "intensity": "LUT 强度；1 = 完全应用，0 = 原图",
            "layout": "LUT 布局：auto 自动识别 / hald / stripe",
            "lut_size": "LUT 每维级数（0 = 自动）",
        },
        notes=["`in` 与 `in2` 的角色不能对调；LUT 图建议 `render_lut_cube` 产出。"],
    ),
    "render_lut_cube": dict(
        title="render_lut_cube · .cube 转 LUT 图",
        summary="读取 .cube 3D LUT 文件，CPU 转换为 HALD LUT 图输出（接 render_lut 的 in2）。",
        desc="把 `.cube` 文本 LUT 烘焙成 HALD LUT 图（DOMAIN 归一化在转换时完成），"
             "输出接 `render_lut` 的 `in2` 完成 GPU 调色。与 `color_grade_lut`（CPU 直接应用）"
             "是同一素材的两条路径。",
        params={"cube_path": ".cube 文件路径"},
    ),
    "render_gauss_dir": dict(
        title="render_gauss_dir · 方向高斯",
        summary="参数化方向高斯：ksize ≤ 99、sigma = 0 时按 OpenCV 公式自动推算，方向可选。",
        desc="任意核尺寸的方向高斯 pass：`sigma` 为 0 时按 OpenCV 公式从核尺寸推算有效 σ，"
             "权重在 shader 内由 σ 计算。横向/纵向/双向组合出可调的二维高斯。",
        params={"ksize": "核长度（≤99，奇数）", "direction": "0 水平 / 1 垂直"},
    ),
    "render_box_dir": dict(
        title="render_box_dir · 方向方框模糊",
        summary="参数化方向方框模糊（水平/垂直 pass）。",
        desc="方向方框模糊：与 `render_gauss_dir` 同构、核为均值。两个方向串联 = 二维均值模糊。",
    ),
    "render_unsharp": dict(
        title="render_unsharp · USM 锐化",
        summary="反锐化掩模：in = 原图、in2 = 模糊图，amount 控制强度。",
        desc="USM 锐化的 GPU 实现：`out = in + amount × (in - in2)`。"
             "`in2` 通常由 `render_gauss_dir` 管线产出——注意本节点是双输入形态，"
             "模糊图要显式连线。",
        params={"amount": "锐化强度"},
    ),
    "render_subtract": dict(
        title="render_subtract · 饱和减法",
        summary="in - in2 的饱和减法：形态学梯度/差分画面的积木。",
        desc="饱和减法 `saturate(in - in2)`。配合 `render_dilate_dir`/`render_erode_dir` 的输出"
             "做形态学梯度（膨胀 - 原图 / 原图 - 腐蚀），或两帧差分。",
    ),
    "render_sobel": dict(
        title="render_sobel · 渲染 Sobel",
        summary="GPU render 侧 Sobel（ksize 1-7），核系数 CPU 算好后 uniform 下发。",
        desc="渲染管线 Sobel：核系数按 OpenCV 冲激响应标定（导数核 conv([1,0,-1],binomial(k-3))）"
             "在 CPU 算好、shader 通用加权循环执行。与 OpenCV 内边距对比误差 ≤1。",
        params={"ksize": "核尺寸 1/3/5/7"},
    ),
    "render_scharr": dict(
        title="render_scharr · 渲染 Scharr",
        summary="GPU render 侧 Scharr 导数（平滑核 [3,10,3] 标定）。",
        desc="渲染管线 Scharr：平滑核按本机 OpenCV 冲激响应实测标定为 [3,10,3]"
             "（教科书 [1,2,1] 是错的），内边距误差 ≤1。",
    ),
    "render_laplacian": dict(
        title="render_laplacian · 渲染拉普拉斯",
        summary="GPU render 侧拉普拉斯（ksize 1-7）。",
        desc="渲染管线拉普拉斯：k=3 为对角八邻域核 [[2,0,2],[0,-8,0],[2,0,2]]（OpenCV 标定），"
             "k=1 为四邻域——与教科书写法不同，以参数表为准。",
    ),
    "render_dilate_dir": dict(
        title="render_dilate_dir · 方向膨胀",
        summary="GPU 分离形态学膨胀（max），iterations 重复 pass 表达多次腐蚀膨胀。",
        desc="方向形态学膨胀（核内取 max）。GPU 上 max/min 天然分离可迭代："
             "`iterations` 控制重复次数；与 `render_erode_dir` 组合表达开/闭/梯度（对照 OpenCV"
             " 全图逐位一致——clamp-to-edge 与 ±inf 边界在 max/min 下等价）。",
    ),
    "render_erode_dir": dict(
        title="render_erode_dir · 方向腐蚀",
        summary="GPU 分离形态学腐蚀（min）。",
        desc="方向形态学腐蚀（核内取 min），与 `render_dilate_dir` 对称；"
             "组合方式见形态学编排范本图。",
    ),

    # ---------- 图层混合（1） ----------
    "blend": dict(
        title="blend · 图层混合（27 种模式）",
        summary="Photoshop 27 种图层混合模式的双输入合成：GPU 优先执行、CPU 镜像兜底。",
        desc="把 `in`（上层）按 `mode` 混合到 `in2`（下层），实现 Photoshop 全套 27 种混合模式"
             "（正常/溶解/变暗族/变亮族/叠加族/差值族/HSL 族）。`opacity` 控制整体不透明度。"
             "`device` 选 auto 时 GPU compute 优先、失败自动落 CPU 镜像实现——结果一致、"
             "环境不挑。溶解类模式的噪声由 `seed` 决定，可复现。",
        params={
            "mode": "混合模式（27 项清单见表后）",
            "opacity": "混合结果的不透明度",
            "device": "执行设备：auto = GPU 优先、失败回退 CPU 镜像实现",
            "seed": "溶解（dissolve）类模式的随机种子",
        },
        notes=["两路输入尺寸须一致；通道数不一致时按混合模式语义处理 alpha。",
               "HSL 族（hue/saturation/color/luminosity）按 Photoshop 传递函数实现。"],
    ),

    # ---------- 人脸检测（1） ----------
    "face_detect": dict(
        title="face_detect · 人脸检测",
        summary="人脸框检测（可开 478 点关键点）：MediaPipe / MNN 双后端，auto 自动降级。",
        desc="检测输入图像中的人脸，输出人脸框；`output_landmarks` 开启后叠加 478 点关键点"
             "（mediapipe_478 方案，两后端统一）。`backend` 为 auto 时 MediaPipe 优先、"
             "不可用自动降级 MNN；实际使用的后端写在结果里可观测。",
        params={
            "model_path": "MNN 后端检测模型路径（空 = 内置查找路径）",
            "landmark_model_path": "关键点模型路径（output_landmarks 时使用）",
            "max_faces": "最多保留的人脸数",
            "score_threshold": "置信度阈值",
            "nms_threshold": "NMS 重叠阈值",
            "output_landmarks": "是否输出 478 点关键点",
        },
        notes=["模型资产缺失时按后端语义返回 FAILED（提示跑下载脚本），不会静默空结果。",
               "TG_FACE_DEBUG=1 环境变量打开后端诊断日志。"],
    ),

    # ---------- 人像抠像（1） ----------
    "matting": dict(
        title="matting · 人像抠像",
        summary="人像 alpha 抠像：三输出（alpha 结果 / 灰度掩膜 / 透明切图），MediaPipe / MNN 双后端。",
        desc="估计输入图像的人像 alpha 通道，三个输出端口各有用途：`out` 为结构化结果"
             "（含 alpha 与后端信息），`mask` 为 0-255 灰度掩膜，`cutout` 为 RGBA 透明切图"
             "（直通 alpha，背景透明）。alpha 经双线性回放对齐原图尺寸。"
             "`backend` 语义同 face_detect：MediaPipe（selfie segmenter）优先、降级 MNN（MODNet）。",
        notes=["`cutout` 可直接接 `gpu_alpha_composite`（in）与背景图（in2）做最终合成。",
               "两后端在标准人像上前景覆盖率交叉一致（±0.1% 量级），可选其一固定使用。",
               "TG_MATTING_DEBUG=1 打开 MNN 诊断日志。"],
    ),
}
