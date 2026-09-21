# -*- coding: utf-8 -*-
"""Node-guide English metadata. Mirrors metadata_zh.py (see its docstring)."""

MODULES = {
    "core": "Core library",
    "image_reader": "Image input",
    "image_writer": "Image output",
    "image_filtering": "OpenCV filtering",
    "image_geometry": "OpenCV geometry",
    "image_color": "OpenCV color & threshold",
    "image_color_grading": "Color grading",
    "image_enhance": "OpenCV enhancement",
    "image_segmentation": "OpenCV segmentation",
    "video_io": "Video I/O",
    "gpu_image_processing": "GPU image processing",
    "render_task": "GPU render",
    "blend": "Layer blend",
    "face_detect": "Face detection",
    "matting": "Image matting",
}

COMMON_PARAMS = {
    "kernel_size": "kernel edge length in pixels; must be a positive odd number",
    "ksize": "kernel edge length in pixels; must be a positive odd number",
    "sigma": "Gaussian sigma; 0 derives it from the kernel size",
    "sigma_x": "horizontal sigma; 0 = auto",
    "sigma_y": "vertical sigma; 0 = same as horizontal",
    "sigma_color": "color-space filter strength (larger = wider color ranges are averaged)",
    "sigma_space": "coordinate-space filter strength (larger = farther pixels influence each other)",
    "dx": "derivative order in x",
    "dy": "derivative order in y",
    "iterations": "number of repetitions",
    "anchor_x": "kernel anchor x; -1 = center",
    "anchor_y": "kernel anchor y; -1 = center",
    "normalize": "normalize by the kernel area",
    "border_type": "border pixel extrapolation mode",
    "interpolation": "interpolation method",
    "depth": "output bit depth (-1 = same as input)",
    "backend": "inference backend; Auto degrades gracefully (MediaPipe → MNN)",
    "delegate": "MediaPipe inference delegate (CPU / GPU)",
    "device": "MNN device (CPU / Metal / Auto)",
    "threads": "CPU inference threads",
    "width": "target width in pixels; 0 follows the input",
    "height": "target height in pixels; 0 follows the input",
    "script_path": "custom render script path (from an effect manifest; empty = built-in effects)",
    "format": "render target texture format",
    "clear": "clear the target at pass start",
    "clear_color": "clear color as comma-separated RGBA components",
    "blend": "enable blended output",
    "opacity": "opacity; 1 = fully opaque, 0 = fully transparent",
}

META = {
    # ---------- Core ----------
    "io_input": dict(
        title="io_input · Graph boundary input",
        summary="Graph boundary for embedded execution: the host binds a value into io_input before running; downstream tasks consume its out port.",
        desc="Graph-boundary input node for embedded SDK hosts: the host writes a bound image/tensor "
             "into this node before execution, and the rest of the graph reads from its `out` port. "
             "You normally don't place it manually in GraphStudio — use `opencv_image_read` "
             "or another source node instead.",
        notes=["Registered directly by the core library; no submodule required."],
    ),
    "io_output": dict(
        title="io_output · Graph boundary output",
        summary="Graph boundary for embedded execution: the host collects the graph result from io_output after the run.",
        desc="Graph-boundary output node: after execution the host collects the result from it "
             "(the upstream port value passes through unchanged). Not needed on the GraphStudio "
             "canvas — use the image viewer or `opencv_image_write` to inspect results.",
        notes=["Registered directly by the core library; no submodule required."],
    ),

    # ---------- Image I/O ----------
    "opencv_image_read": dict(
        title="opencv_image_read · Read image",
        summary="Reads one image from disk (png / jpg / bmp / tiff / webp …) as the graph's input source.",
        desc="Reads an image and outputs it on `out` — the standard graph input source, supporting "
             "every format the OpenCV codecs cover. Output is BGR by default; enabling `keep_alpha` "
             "keeps the alpha channel (4-channel RGBA) for blend/matting downstream tasks.",
        params={
            "file_path": "image path; relative paths resolve against the graph directory "
                         "(probing it and its ancestors), absolute paths are used as-is",
        },
        notes=["A missing path or decode failure fails the task with a readable reason in the log panel."],
    ),
    "opencv_image_write": dict(
        title="opencv_image_write · Write image",
        summary="Writes the upstream image to a file; the format is inferred from the extension.",
        desc="Writes the image received on the input port to `file_path`. The output format follows "
             "the file extension; alpha survives in formats that support it (e.g. PNG).",
        params={
            "file_path": "output path; relative paths join the graph directory directly "
                         "(no asset probing, so original assets can't be clobbered)",
        },
    ),

    # ---------- OpenCV filtering ----------
    "opencv_blur_filter": dict(
        title="opencv_blur_filter · Box blur",
        summary="Box-style mean blur: averages the kernel window — the fastest way to smooth.",
        desc="Standard mean blur (`cv::blur`). Larger kernels smooth more, erasing detail along "
             "with noise; commonly used to pre-filter before downsampling or for light denoising.",
    ),
    "opencv_gaussian_blur_filter": dict(
        title="opencv_gaussian_blur_filter · Gaussian blur",
        summary="Gaussian-weighted blur: natural smoothing, the default choice for denoising.",
        desc="Gaussian blur (`cv::GaussianBlur`). With `sigma` = 0 the standard deviation is derived "
             "from the kernel size. Semantically mirrored on the GPU render side by "
             "`render_gauss_h` / `render_gauss_v` (separable passes).",
    ),
    "opencv_median_blur_filter": dict(
        title="opencv_median_blur_filter · Median blur",
        summary="Median filter: takes the median in the window — extremely effective on salt-and-pepper noise, edge-preserving.",
        desc="Median filter (`cv::medianBlur`). Far better than linear filters on impulse noise "
             "(salt & pepper) and it doesn't smear edges like the mean does, at a higher compute cost.",
    ),
    "opencv_bilateral_filter": dict(
        title="opencv_bilateral_filter · Bilateral filter",
        summary="Edge-preserving denoise: weights by spatial distance and color difference together.",
        desc="Bilateral filter (`cv::bilateralFilter`). `sigma_color` decides how large a color "
             "difference still counts as the same color; `sigma_space` sets the spatial reach — "
             "together they set the smoothing strength. A staple for skin smoothing and cartoon-like preprocessing.",
    ),
    "opencv_box_filter": dict(
        title="opencv_box_filter · Box filter",
        summary="Box filter: sums the kernel window (optionally normalized) — the generic form of the mean blur.",
        desc="Box filter (`cv::boxFilter`). With `normalize` on it equals the mean blur; with it off "
             "the output is the sum of window pixels (an intermediate form for integral-style math).",
    ),
    "opencv_sobel_filter": dict(
        title="opencv_sobel_filter · Sobel derivative",
        summary="Sobel gradient with selectable dx/dy orders — the basic edge-detection building block.",
        desc="Sobel derivative (`cv::Sobel`). `dx`/`dy` pick direction and order (their sum must be "
             "below the kernel size); the magnitude output typically feeds `opencv_threshold` "
             "for binarized edges.",
    ),
    "opencv_scharr_filter": dict(
        title="opencv_scharr_filter · Scharr derivative",
        summary="Scharr operator: more rotationally accurate than Sobel at the 3× kernel size.",
        desc="Scharr derivative (`cv::Scharr`). Only a 3×3 kernel, but with less angular error than "
             "same-size Sobel; swap it in when you need precise gradients or contour extraction.",
    ),
    "opencv_laplacian_filter": dict(
        title="opencv_laplacian_filter · Laplacian",
        summary="Second-derivative operator: isotropic edge/texture response, used for sharpening and edge detection.",
        desc="Laplacian (`cv::Laplacian`). Noise-sensitive — usually blur first, then take the "
             "Laplacian (LoG style). ksize=1 is the classic 4-neighborhood kernel.",
    ),
    "opencv_filter_2d": dict(
        title="opencv_filter_2d · Custom convolution",
        summary="2D convolution with any kernel you write down as a numeric matrix.",
        desc="Generic 2D convolution (`cv::filter2D`). The kernel is given as a string "
             "(commas within rows, semicolons between rows, e.g. `0,-1,0;-1,5,-1;0,-1,0`) — "
             "ideal for experimenting with custom operators without writing code.",
        params={"kernel": "kernel matrix: rows separated by semicolons, values by commas"},
    ),
    "opencv_sep_filter_2d": dict(
        title="opencv_sep_filter_2d · Separable convolution",
        summary="Two 1D kernels applied in sequence (horizontal then vertical) — the fast filter2d for big kernels.",
        desc="Separable convolution (`cv::sepFilter2D`). Split a 2D kernel into row/column 1D "
             "kernels; compute drops from k² to 2k — the right way to do large Gaussian-style filters.",
        params={
            "kernel_x": "horizontal 1D kernel (comma-separated values)",
            "kernel_y": "vertical 1D kernel (comma-separated values)",
        },
    ),
    "opencv_sqr_box_filter": dict(
        title="opencv_sqr_box_filter · Squared box filter",
        summary="Averaged squared pixel values over the window — a building block for local energy/variance.",
        desc="Squared box filter (`cv::sqrBoxFilter`). Each output is the (normalized) sum of squared "
             "pixels in the window; combined with `opencv_box_filter` it yields local variance "
             "for texture/noise analysis.",
    ),
    "opencv_gabor_filter": dict(
        title="opencv_gabor_filter · Gabor filter",
        summary="Directional texture filtering with Gabor kernels — a classic for fingerprint/fabric analysis.",
        desc="Gabor filtering (`cv::getGaborKernel` + filter2D). Builds a sinusoid-modulated Gaussian "
             "kernel from wavelength/direction/bandwidth that responds strongest to a specific "
             "orientation and frequency; banks of them form texture features.",
        params={
            "ksize_x": "kernel width in pixels", "ksize_y": "kernel height in pixels",
            "sigma": "Gaussian envelope sigma",
            "theta": "filter orientation in radians",
            "lambd": "sinusoid wavelength in pixels",
            "gamma": "spatial aspect ratio",
            "psi": "phase offset in radians",
        },
    ),
    "opencv_dilate": dict(
        title="opencv_dilate · Dilate",
        summary="Morphological dilation: max over the window — bright regions grow, gaps close.",
        desc="Morphological dilation (`cv::dilate`). White regions expand by the kernel shape; "
             "used to bridge broken strokes and beef up bright targets. Inverse of `opencv_erode`.",
    ),
    "opencv_erode": dict(
        title="opencv_erode · Erode",
        summary="Morphological erosion: min over the window — bright regions shrink, white specks vanish.",
        desc="Morphological erosion (`cv::erode`). White regions shrink by the kernel; removes small "
             "bright noise and separates touching objects. Inverse of `opencv_dilate`.",
    ),
    "opencv_morphology_ex": dict(
        title="opencv_morphology_ex · Morphology combinations",
        summary="Open/close/gradient/top-hat/black-hat: the composed morphology operations in one node.",
        desc="Composed morphology (`cv::morphologyEx`): open removes white specks, close fills dark "
             "holes, gradient gives morphological edges, top-hat/black-hat extract structures brighter/"
             "darker than their neighborhood. The GPU render side expresses the same math by orchestrating "
             "`render_dilate_dir` + `render_erode_dir` passes.",
    ),
    "opencv_pyr_down": dict(
        title="opencv_pyr_down · Pyr down",
        summary="Gaussian blur + halved size: anti-aliased downsampling.",
        desc="Pyramid downsample (`cv::pyrDown`). Gaussian-blurs then drops every other row/column, "
             "halving the size — far less moiré than a direct resize.",
    ),
    "opencv_pyr_up": dict(
        title="opencv_pyr_up · Pyr up",
        summary="Upscale ×2 then blur: the pyramid inverse.",
        desc="Pyramid upsample (`cv::pyrUp`). Doubles the size then applies a Gaussian; the inverse "
             "of `opencv_pyr_down` (a round trip loses high frequencies).",
    ),

    # ---------- OpenCV geometry ----------
    "opencv_resize": dict(
        title="opencv_resize · Resize",
        summary="Resize by target width/height and interpolation method.",
        desc="Image resize (`cv::resize`). Use `INTER_AREA` when shrinking (anti-moiré), "
             "`INTER_LINEAR`/`INTER_CUBIC` when enlarging; `INTER_NEAREST` preserves exact values "
             "(mandatory for masks/label maps).",
    ),
    "opencv_flip": dict(
        title="opencv_flip · Flip",
        summary="Horizontal / vertical / both-axes flip (flip code 0 / 1 / -1).",
        desc="Mirrored flip (`cv::flip`). `flip_code` 1 = horizontal, 0 = vertical, "
             "-1 = both (= 180° rotation). Common for data augmentation and selfie mirroring.",
        params={"flip_code": "flip direction: 1 = horizontal, 0 = vertical, -1 = both"},
    ),
    "opencv_rotate": dict(
        title="opencv_rotate · Rotate 90°",
        summary="Fast 90°/180°/270° rotation (rotate code 0/1/2).",
        desc="Right-angle rotation (`cv::rotate`). Only multiples of 90°, with zero interpolation "
             "loss; arbitrary angles go through `opencv_warp_affine`.",
        params={"rotate_code": "rotation: 0 = 90° clockwise, 1 = 180°, 2 = 270° clockwise"},
    ),
    "opencv_warp_affine": dict(
        title="opencv_warp_affine · Affine warp",
        summary="Translate/rotate/scale/shear by a 2×3 affine matrix; arbitrary-angle rotation lives here.",
        desc="Affine warp (`cv::warpAffine`). The matrix is a six-number string (row-major, e.g. "
             "translation `1,0,20;0,1,10`); anything that preserves parallel lines can be expressed.",
        params={"matrix": "2×3 matrix: semicolons between rows, commas within"},
    ),
    "opencv_transpose": dict(
        title="opencv_transpose · Transpose",
        summary="Swaps rows and columns (mirror across the main diagonal).",
        desc="Matrix transpose (`cv::transpose`). Width and height swap; equals a mirror across "
             "the main diagonal.",
    ),

    # ---------- OpenCV color & threshold ----------
    "opencv_cvt_color": dict(
        title="opencv_cvt_color · Color conversion",
        summary="Common conversions among BGR/GRAY/RGB/HSV/HLS/LAB/LUV/YCrCb/XYZ.",
        desc="Color-space conversion (`cv::cvtColor`), direction picked by the `code` enum. "
             "Convert to HSV before `opencv_threshold` to segment by brightness/saturation — "
             "a permanent fixture in grading and masking flows.",
        notes=["On 8-bit images OpenCV's HSV hue range is 0-179, not 0-359."],
    ),
    "opencv_threshold": dict(
        title="opencv_threshold · Threshold",
        summary="Global threshold with binary/inverted/trunc/tozero semantics, plus OTSU and TRIANGLE auto modes.",
        desc="Fixed threshold (`cv::threshold`). `type` picks binary/inverted/truncate/to-zero "
             "semantics; `OTSU_BINARY` / `TRIANGLE_BINARY` ignore `thresh` and derive the threshold "
             "automatically.",
        notes=["OTSU/TRIANGLE ignore `thresh`; the input must be 8-bit single channel (grayscale first)."],
    ),
    "opencv_adaptive_threshold": dict(
        title="opencv_adaptive_threshold · Adaptive threshold",
        summary="Binarizes by local neighborhood statistics (mean/Gaussian) — robust to uneven lighting.",
        desc="Adaptive threshold (`cv::adaptiveThreshold`). The per-pixel threshold comes from the "
             "neighborhood mean (or Gaussian-weighted mean) minus a constant C — right for document "
             "scans and shadowed scenes with lighting gradients.",
        params={
            "block_size": "neighborhood edge length (positive odd)",
            "c": "constant subtracted from the local mean; larger = stricter",
        },
    ),
    "opencv_apply_color_map": dict(
        title="opencv_apply_color_map · Color map",
        summary="Maps single-channel input through a preset palette (JET / VIRIDIS / BONE …) — heatmaps in one step.",
        desc="False-color mapping (`cv::applyColorMap`). Grayscale/depth/confidence maps get colored "
             "by the `colormap` enum, outputting a 3-channel visualization.",
    ),

    # ---------- Color grading ----------
    "color_grade_wheels": dict(
        title="color_grade_wheels · Color wheels",
        summary="Lift / Gamma / Gain wheels: RGB offset plus master brightness per tonal range.",
        desc="DaVinci-style three-wheel grading: `lift` (shadows), `gamma` (midtones) and `gain` "
             "(highlights), each an RGB offset plus a master brightness. Given as comma-separated "
             "RGB + luminance components per wheel.",
        params={
            "lift": "shadows wheel: R,G,B offset + luminance",
            "gamma": "midtones wheel: R,G,B offset + luminance",
            "gain": "highlights wheel: R,G,B offset + luminance",
        },
    ),
    "color_grade_curves": dict(
        title="color_grade_curves · Curves",
        summary="Master and per-channel curves described as control-point strings.",
        desc="Photoshop-style curve grading. Each curve is a control-point list (x,y pairs, "
             "comma-separated) interpolated into a 256-entry LUT. The master curve shapes tone, "
             "the R/G/B curves shape color cast.",
        params={
            "master": "master control points: x1,y1,x2,y2,…",
            "r": "red channel control points", "g": "green channel control points",
            "b": "blue channel control points",
        },
    ),
    "color_grade_lut": dict(
        title="color_grade_lut · LUT grading",
        summary="Applies a .cube 3D LUT (trilinear or tetrahedral interpolation).",
        desc="Reads a 3D LUT file (`.cube`) and applies it. `interpolation` picks trilinear (fast) "
             "or tetrahedral (smoother). Photography grading packs ship as .cube files — drop one in.",
        params={
            "lut_file": "LUT file path (.cube); relative paths probe the graph directory",
            "interpolation": "TRILINEAR / TETRAHEDRAL",
        },
        notes=["The GPU-render equivalents are `render_lut_cube` (.cube → HALD image) + `render_lut` "
               "(apply) — faster for bulk work."],
    ),
    "color_grade_bcs": dict(
        title="color_grade_bcs · Brightness/Contrast/Saturation",
        summary="Brightness, contrast and saturation in a single node.",
        desc="The classic BCS trio: `brightness` shifts levels, `contrast` stretches them apart, "
             "`saturation` adjusts color intensity. Neutral values follow the defaults in the table.",
    ),
    "color_grade_mixer": dict(
        title="color_grade_mixer · Channel mixer",
        summary="Recombines RGB output channels from weighted input channels — channel swaps and tonal splits.",
        desc="Channel mixer: each output channel is a weighted sum of the R/G/B inputs "
             "(a 3×3 weights string). Classic moves include red/cyan swaps, custom black-and-white "
             "weights, and pseudo-infrared tones.",
        params={"matrix": "3×3 weights: semicolons between rows (out_R = first row)"},
    ),
    "color_grade_hsl": dict(
        title="color_grade_hsl · HSL qualifier",
        summary="Per-hue-range (red/yellow/green/cyan/blue/magenta) hue/sat/lum adjustments plus master controls.",
        desc="HSL-zoned grading: pick a hue range, then shift its hue, saturation and luminance "
             "independently; master saturation/luminance ride along. Six zones × 3 components "
             "are packed into the adjustments string.",
        params={"adjustments": "six zones, each a hue,sat,lum triple"},
    ),

    # ---------- OpenCV enhancement ----------
    "opencv_equalize_hist": dict(
        title="opencv_equalize_hist · Histogram equalization",
        summary="Global histogram equalization: stretches dynamic range.",
        desc="Histogram equalization (`cv::equalizeHist`). Flattens the gray distribution of an "
             "8-bit single-channel image — a quick fix for low-contrast photos and medical imagery. "
             "For color images, split channels or work in a YUV-like space on luminance.",
    ),
    "opencv_clahe": dict(
        title="opencv_clahe · CLAHE",
        summary="Contrast-limited adaptive histogram equalization: local enhancement with controllable noise amplification.",
        desc="CLAHE (`cv::createCLAHE`). Equalizes per `tile` grid with `clip_limit` capping the "
             "histogram to keep noise down — the workhorse for medical and night-scene enhancement.",
        params={
            "clip_limit": "contrast limiting strength (1-4 typical)",
            "tile_grid_size": "tile grid (e.g. 8x8; see the default format)",
        },
    ),
    "opencv_sharpen": dict(
        title="opencv_sharpen · Sharpen",
        summary="Classic unsharp-mask sharpening wrapped in one node.",
        desc="Sharpening filter: the classic USM — original + amount × (original − blurred). "
             "`amount` sets the strength; over-sharpening amplifies noise and halos.",
        params={"amount": "sharpen strength"},
    ),
    "opencv_denoise": dict(
        title="opencv_denoise · Non-local means",
        summary="fastNlMeansDenoising: patch-similarity denoise with the best detail retention.",
        desc="Non-local means (`cv::fastNlMeansDenoising`). Aggregates similar patches instead of "
             "smoothing locally — better Gaussian-noise removal and detail retention than the "
             "bilateral filter, at a higher compute cost. Larger `h` denoises harder.",
        params={"h": "filter strength (luminance); start around 10"},
    ),
    "opencv_add_weighted": dict(
        title="opencv_add_weighted · Weighted blend",
        summary="Linear blend of two inputs by alpha/beta/gamma — stacking, crossfades, watermarks.",
        desc="Linear blend (`cv::addWeighted`): `out = alpha·in + beta·in2 + gamma`. Two input "
             "ports; typical for exposure blending, watermark overlays and animated transitions.",
        params={"alpha": "weight of the first input", "beta": "weight of the second input",
                "gamma": "additive offset"},
        notes=["Both inputs need identical size and channel count."],
    ),
    "opencv_gamma_correct": dict(
        title="opencv_gamma_correct · Gamma correction",
        summary="Power-law adjustment (out = in^gamma) — display matching and lifting shadows.",
        desc="Gamma correction via LUT. `gamma` < 1 lifts shadows, > 1 darkens; linear→sRGB "
             "conversion (and back) is the same node.",
        params={"gamma": "exponent; ~0.45 for linear→sRGB"},
    ),
    "opencv_brightness_contrast": dict(
        title="opencv_brightness_contrast · Brightness/contrast",
        summary="Linear gain/bias adjustment of brightness and contrast.",
        desc="Linear tonal adjustment: `out = in * contrast + brightness`. The most common "
             "look-tuning node; usually sits before BCS in a grading chain for base correction.",
    ),
    "opencv_invert": dict(
        title="opencv_invert · Invert",
        summary="Pixel inversion (255 - x): negatives and mask inversion.",
        desc="Inversion (`bitwise_not` semantics). Negatives and mask flips (foreground↔background) "
             "in one step.",
    ),

    # ---------- OpenCV segmentation ----------
    "opencv_grabcut": dict(
        title="opencv_grabcut · GrabCut",
        summary="Iterated graph-cut segmentation initialized by a rectangle or mask — the classic interactive cutout.",
        desc="GrabCut (`cv::grabCut`). `init_mode` picks rectangle (a box containing the target) or "
             "mask (rough fg/bg marks); iterated energy minimization refines a precise foreground "
             "mask. For high-quality portraits prefer the `matting` node.",
        notes=["RECT mode uses rect_x/y/width/height; MASK mode ignores them.",
               "More iterations refine edges and cost time; 5-10 usually suffices."],
    ),
    "opencv_watershed": dict(
        title="opencv_watershed · Watershed",
        summary="Classic watershed with a marker image to split touching objects.",
        desc="Watershed (`cv::watershed`). Needs the input image plus a marker map (distinct integer "
             "labels as seeds, 0 = unknown); flooding over the gradient terrain yields boundaries. "
             "Upstream, `opencv_threshold` + `opencv_connected_components` typically build the markers.",
    ),
    "opencv_flood_fill": dict(
        title="opencv_flood_fill · Flood fill",
        summary="Floods from a seed point by similarity — selections and magic-wand behavior.",
        desc="Flood fill (`cv::floodFill`). From (seed_x, seed_y), replaces the connected region "
             "within lo_diff/up_diff color tolerance with `new_value` and reports the filled mask.",
        params={
            "seed_x": "seed point x", "seed_y": "seed point y",
            "lo_diff": "lower tolerance", "up_diff": "upper tolerance",
            "new_value": "fill color",
        },
    ),
    "opencv_connected_components": dict(
        title="opencv_connected_components · Connected components",
        summary="Labels each connected region of a binary image with an integer id.",
        desc="Connected-component analysis (`cv::connectedComponents`). 8- or 4-connectivity; "
             "outputs a per-pixel integer label map. Combine with statistics for counting and "
             "small-area noise removal.",
        params={"connectivity": "8 or 4"},
    ),
    "opencv_distance_transform": dict(
        title="opencv_distance_transform · Distance transform",
        summary="Distance field of foreground pixels to the nearest background — the intermediate for skeletons and width.",
        desc="Distance transform (`cv::distanceTransform`). Each non-zero pixel gets its distance "
             "to the nearest zero pixel, with selectable distance type and mask size; the classic "
             "pre-pass for watershed seeding and stroke-width estimation.",
    ),

    # ---------- Video I/O ----------
    "video_reader": dict(
        title="video_reader · Read video",
        summary="Reads video files frame by frame (mp4 / mov / avi …); each execution yields one frame.",
        desc="Video source node: the executor drives it frame by frame — each `execute()` outputs "
             "the next frame on `out`, with teardown at stream end. `api_preference` forces a decode "
             "backend (automatic by default; FFMPEG is the most common).",
        params={
            "file_path": "video path; relative paths probe the graph directory",
            "api_preference": "decode backend: DEFAULT (auto) / FFMPEG",
        },
        notes=["Frame semantics are executor-driven: one GraphStudio execution = one frame; use "
               "embedded SDK execution or a script loop for batch transcoding."],
    ),
    "video_writer": dict(
        title="video_writer · Write video",
        summary="Encodes an incoming frame sequence into a video file (fourcc / fps / color configurable).",
        desc="Video sink node: each upstream frame is written; the file closes and the trailer is "
             "flushed when the graph run finishes. `fourcc` is a four-character codec name "
             "(e.g. `mp4v`); `fps` must match the source.",
        params={
            "file_path": "output path (extension picks the container)",
            "fourcc": "four-character codec id, e.g. mp4v / avc1",
            "fps": "frame rate; mismatch with the source speeds up/slows down playback",
            "is_color": "write color frames",
        },
    ),

    # ---------- GPU image processing ----------
    "gpu_box_blur": dict(
        title="gpu_box_blur · GPU box blur",
        summary="Box mean blur on the GPU compute pipeline, semantics aligned with opencv_blur_filter.",
        desc="Mean blur on the GPU compute backend (wgpu/Metal/Vulkan), bit-aligned with the CPU "
             "reference. An order of magnitude faster for bulk processing; parameters match "
             "`opencv_blur_filter`.",
        notes=["Fails the task when the GPU backend can't init (no silent CPU fallback; use blend's "
               "device=auto when you need fallback semantics)."],
    ),
    "gpu_gaussian_blur": dict(
        title="gpu_gaussian_blur · GPU Gaussian blur",
        summary="Gaussian blur as a GPU compute op (WGSL/MSL/GLSL single-source kernels).",
        desc="Gaussian blur on GPU, semantics aligned with `opencv_gaussian_blur_filter`. Output "
             "stays GPU-resident and chains into other gpu_* nodes with zero copies.",
    ),
    "gpu_grayscale": dict(
        title="gpu_grayscale · GPU grayscale",
        summary="GPU RGB→gray using the BGR2GRAY fixed-point coefficients; no parameters.",
        desc="GPU grayscale via OpenCV's BGR2GRAY fixed-point coefficients (4899R+9617G+1868B), "
             "bit-identical to CPU within unorm precision.",
    ),
    "gpu_brightness_contrast": dict(
        title="gpu_brightness_contrast · GPU brightness/contrast",
        summary="Linear brightness/contrast adjustment on the GPU.",
        desc="GPU version of the linear tonal adjustment, semantics of `opencv_brightness_contrast`. "
             "A permanent node in realtime preview chains.",
    ),
    "gpu_resize": dict(
        title="gpu_resize · GPU resize",
        summary="Bilinear resize on the GPU.",
        desc="GPU bilinear resize. When you need nearest-neighbor or other interpolation semantics, "
             "use the CPU `opencv_resize`.",
    ),
    "gpu_threshold": dict(
        title="gpu_threshold · GPU threshold",
        summary="Fixed-threshold binarization on the GPU.",
        desc="GPU fixed threshold, matching `opencv_threshold` in BINARY mode; for OTSU auto "
             "thresholding use the CPU version.",
    ),
    "gpu_gamma": dict(
        title="gpu_gamma · GPU gamma",
        summary="Power-law gamma correction on the GPU.",
        desc="GPU gamma (computed directly as a power on the GPU), semantics of "
             "`opencv_gamma_correct`.",
    ),
    "gpu_invert": dict(
        title="gpu_invert · GPU invert",
        summary="GPU pixel inversion; no parameters.",
        desc="GPU inversion (1 - x in normalized coordinates), semantics of `opencv_invert`.",
    ),
    "gpu_rgb2hsv": dict(
        title="gpu_rgb2hsv · GPU RGB→HSV",
        summary="RGB→HSV conversion on the GPU; no parameters.",
        desc="GPU RGB→HSV. Note the GPU output normalizes hue to [0,1] (OpenCV 8-bit images use "
             "0-179), so downstream comparison thresholds must be scaled accordingly.",
    ),
    "gpu_flip": dict(
        title="gpu_flip · GPU flip",
        summary="Image flip on the GPU (horizontal/vertical/both).",
        desc="GPU flip: coordinate swap in the shader per flip code, semantics of `opencv_flip`.",
    ),
    "gpu_rotate90": dict(
        title="gpu_rotate90 · GPU rotate 90°",
        summary="Multiple-of-90° rotation on the GPU.",
        desc="GPU right-angle rotation via coordinate remapping (no interpolation loss), semantics "
             "of `opencv_rotate`.",
    ),
    "gpu_crop": dict(
        title="gpu_crop · GPU crop",
        summary="Rectangular crop on the GPU (x/y/width/height).",
        desc="GPU crop: extracts the (x, y, width, height) rectangle; the output size follows.",
    ),
    "gpu_sharpen": dict(
        title="gpu_sharpen · GPU sharpen",
        summary="Unsharp-mask sharpening on the GPU.",
        desc="GPU sharpening, semantics of `opencv_sharpen`; `amount` sets the strength.",
    ),
    "gpu_sobel": dict(
        title="gpu_sobel · GPU Sobel",
        summary="GPU Sobel gradient (magnitude output).",
        desc="GPU Sobel with magnitude semantics aligned to `convertScaleAbs` (|gx|/|gy| each "
             "saturated, then a 0.5-weighted blend). Kernel coefficients are calibrated against "
             "the CPU reference; interior error ≤1/255.",
    ),
    "gpu_laplacian": dict(
        title="gpu_laplacian · GPU Laplacian",
        summary="GPU second-derivative Laplacian; no parameters.",
        desc="GPU Laplacian (4-neighborhood kernel), aligned with `opencv_laplacian_filter` at ksize=1.",
    ),
    "gpu_blend": dict(
        title="gpu_blend · GPU two-input blend",
        summary="Linear blend of two inputs on the GPU (opacity interpolation).",
        desc="GPU two-input linear blend: `out = mix(in, in2, opacity)`. This is NOT the same node "
             "as `blend` (the Photoshop 27-mode submodule) — that one is richer and carries a CPU "
             "fallback.",
        params={"opacity": "blend weight of in2"},
    ),
    "gpu_alpha_composite": dict(
        title="gpu_alpha_composite · GPU alpha composite",
        summary="GPU alpha compositing (over): foreground in over background in2.",
        desc="GPU alpha composite: `in` (alpha-carrying foreground) over `in2` (background). "
             "Feeds matting/cutout RGBA straight into the final composition.",
        notes=["`in2` must be 3-channel (BGR); both inputs must match in size.",
               "Get 4-channel input via `opencv_image_read` keep_alpha or matting's cutout output."],
    ),

    # ---------- GPU render ----------
    "render_pass": dict(
        title="render_pass · Render pass",
        summary="The single-pass offscreen building block: one script, one fullscreen-triangle draw.",
        desc="The minimal unit of graph-level render orchestration: one pass = load the effect "
             "script → optional clear → one fullscreen-triangle draw. Compose multi-pass work with "
             "`render_pipeline`; this node suits single-effect debugging and quick previews.",
        notes=["Output stays GPU-resident and chains into compute/render nodes without downloads."],
        params={"script_path": "script path prefix (.metal / .vert+.frag / .wgsl); empty = built-in passthrough"},
    ),
    "render_pipeline": dict(
        title="render_pipeline · Multi-pass render pipeline",
        summary="Task-level pass orchestration: a passes count plus flat pass{i}_* parameter keys express the whole filter chain.",
        desc="Chains N render passes inside one task: `passes` declares the count; each pass's "
             "effect and parameters are flat keys like `pass1_effect`, `pass1_intensity` … "
             "(TaskParams is a flat map — nested structures don't fit). Composite filters "
             "(2D Gaussian = horizontal + vertical; morphological open/close = erode + dilate) are "
             "expressed this way rather than as new composite task types.",
        params={
            "passes": "number of passes (1-64)",
            "width": "output canvas width; 0 follows the input",
            "height": "output canvas height; 0 follows the input",
            "effects_path": "effect manifest (directory) path; empty for built-ins",
        },
        notes=["pass{i}_effect names the effect for each segment; other pass{i}_<param> keys pass through.",
               "Runnable example graphs live in `submodules/render/render_task/tests/graphs/`."],
    ),
    "render_gradient": dict(
        title="render_gradient · Gradient",
        summary="Generates a linear gradient with no input: tests, backgrounds, compositing plates.",
        desc="Renders a parameterized linear gradient — a zero-input node. Often the signal source "
             "when debugging render-chain shaders, or a clean plate for composites.",
    ),
    "render_passthrough": dict(
        title="render_passthrough · Passthrough",
        summary="Identity render: in comes out unchanged — the control node for the GPU upload/download path.",
        desc="Identity rendering: upload to texture and download again; verifies the render path "
             "and format conversions. The do-nothing control while debugging render chains.",
    ),
    "render_mix": dict(
        title="render_mix · Render mix",
        summary="Two-input GPU mix (factor interpolation) on the render path.",
        desc="Render-side two-input mix: interpolates between `in` and `in2` by the mix factor — "
             "close to `gpu_blend` in semantics but on the render pipeline (extensible with "
             "custom scripts).",
    ),
    "render_gauss_h": dict(
        title="render_gauss_h · Horizontal Gaussian (5-tap)",
        summary="Built-in 5-tap [1,4,6,4,1]/16 separable Gaussian, horizontal pass.",
        desc="Fixed 5-tap binomial Gaussian, horizontal. Chain with `render_gauss_v` for a 2D "
             "Gaussian (equivalent to gauss_dir × 2 in a pipeline).",
    ),
    "render_gauss_v": dict(
        title="render_gauss_v · Vertical Gaussian (5-tap)",
        summary="Built-in 5-tap [1,4,6,4,1]/16 separable Gaussian, vertical pass.",
        desc="Fixed 5-tap binomial Gaussian, vertical — the partner of `render_gauss_h`.",
    ),
    "render_threshold": dict(
        title="render_threshold · Render threshold",
        summary="Luminance threshold binarization on the GPU render path.",
        desc="Render-pipeline thresholding: pixels below the threshold go to 0. Same semantics as "
             "compute-side `gpu_threshold`, but texture-resident output chains into further "
             "render effects.",
    ),
    "render_lut": dict(
        title="render_lut · GPU LUT apply",
        summary="Two-input LUT apply: in = image, in2 = LUT image; HALD and stripe layouts auto-detected.",
        desc="Applies a LUT image (second input) on the GPU: `layout` auto-detects both HALD "
             "N²×N² and stripe N²×N layouts; 8-corner texel-center sampling with manual "
             "trilinear filtering; `intensity` blends the strength. Produce the LUT image with "
             "`render_lut_cube` from a .cube file, or use any HALD color-chart asset.",
        params={
            "intensity": "LUT strength; 1 = fully applied, 0 = original",
            "layout": "LUT layout: auto / hald / stripe",
            "lut_size": "LUT size per dimension (0 = auto)",
        },
        notes=["`in` and `in2` roles are not interchangeable; prefer LUT images produced by `render_lut_cube`."],
    ),
    "render_lut_cube": dict(
        title="render_lut_cube · .cube to LUT image",
        summary="Reads a .cube 3D LUT and converts it on the CPU into a HALD LUT image (feeds render_lut's in2).",
        desc="Bakes a `.cube` text LUT into a HALD LUT image (DOMAIN normalization included), "
             "output feeding `render_lut`'s `in2` for GPU grading. Same material as "
             "`color_grade_lut` (direct CPU apply) via the other path.",
        params={"cube_path": ".cube file path"},
    ),
    "render_gauss_dir": dict(
        title="render_gauss_dir · Directional Gaussian",
        summary="Parameterized directional Gaussian: ksize ≤ 99, sigma = 0 auto-derives per the OpenCV formula.",
        desc="Directional Gaussian pass of any kernel size: with `sigma` = 0 the effective σ is "
             "derived from the kernel size per the OpenCV formula, and weights are computed in "
             "the shader from σ. Combine directions for an adjustable 2D Gaussian.",
        params={"ksize": "kernel length (≤99, odd)", "direction": "0 horizontal / 1 vertical"},
    ),
    "render_box_dir": dict(
        title="render_box_dir · Directional box blur",
        summary="Parameterized directional box blur (horizontal/vertical pass).",
        desc="Directional box blur: same shape as `render_gauss_dir` with a mean kernel. Two "
             "directions chained = a 2D mean blur.",
    ),
    "render_unsharp": dict(
        title="render_unsharp · USM sharpen",
        summary="Unsharp mask: in = original, in2 = blurred; amount controls strength.",
        desc="GPU USM: `out = in + amount × (in - in2)`. `in2` usually comes from a "
             "`render_gauss_dir` pipeline — note this node is two-input; wire the blur explicitly.",
        params={"amount": "sharpen strength"},
    ),
    "render_subtract": dict(
        title="render_subtract · Saturated subtract",
        summary="Saturating in - in2: the building block for morphological gradients and diff mattes.",
        desc="Saturating subtraction `saturate(in - in2)`. Combined with `render_dilate_dir`/"
             "`render_erode_dir` outputs it forms morphological gradients (dilate − original, "
             "original − erode), or frame differencing.",
    ),
    "render_sobel": dict(
        title="render_sobel · Render Sobel",
        summary="Render-path Sobel (ksize 1-7); kernel coefficients computed on the CPU and sent as uniforms.",
        desc="Render-pipeline Sobel: coefficients calibrated to the OpenCV impulse response "
             "(derivative kernel = conv([1,0,-1], binomial(k-3))) computed on the CPU, executed "
             "by the shader's generic weighted loop. Interior error vs OpenCV ≤1.",
        params={"ksize": "kernel size 1/3/5/7"},
    ),
    "render_scharr": dict(
        title="render_scharr · Render Scharr",
        summary="Render-path Scharr derivative (smoothing kernel calibrated as [3,10,3]).",
        desc="Render-pipeline Scharr: the smoothing kernel is calibrated from the local OpenCV "
             "impulse response as [3,10,3] (the textbook [1,2,1] is wrong); interior error ≤1.",
    ),
    "render_laplacian": dict(
        title="render_laplacian · Render Laplacian",
        summary="Render-path Laplacian (ksize 1-7).",
        desc="Render-pipeline Laplacian: k=3 is the diagonal 8-neighborhood kernel "
             "[[2,0,2],[0,-8,0],[2,0,2]] (OpenCV calibration), k=1 the 4-neighborhood — differing "
             "from textbook writeups; trust the parameter table.",
    ),
    "render_dilate_dir": dict(
        title="render_dilate_dir · Directional dilate",
        summary="GPU separable morphological dilation (max); iterations repeat passes.",
        desc="Directional morphological dilation (max over the kernel). max/min are naturally "
             "separable and iterable on the GPU: `iterations` repeats the pass. Combine with "
             "`render_erode_dir` for open/close/gradient (bit-identical to OpenCV over the whole "
             "image — clamp-to-edge equals the ±inf border under max/min).",
    ),
    "render_erode_dir": dict(
        title="render_erode_dir · Directional erode",
        summary="GPU separable morphological erosion (min).",
        desc="Directional morphological erosion (min over the kernel), symmetric to "
             "`render_dilate_dir`; see the morphology orchestration example graphs.",
    ),

    # ---------- Layer blend ----------
    "blend": dict(
        title="blend · Layer blend (27 modes)",
        summary="Two-input compositing with all 27 Photoshop blend modes: GPU-first with a mirrored CPU fallback.",
        desc="Blends `in` (upper layer) onto `in2` (lower layer) by `mode`, implementing the full "
             "Photoshop set of 27 blend modes (normal/dissolve/darken family/lighten family/"
             "overlay family/difference family/HSL family). `opacity` sets overall opacity. With "
             "`device` = auto the GPU compute path runs first and falls back to the mirrored CPU "
             "implementation on failure — identical results on any environment. Dissolve-mode "
             "noise is reproducible via `seed`.",
        params={
            "mode": "blend mode (27 options, listed below the table)",
            "opacity": "opacity of the blended result",
            "device": "execution device: auto = GPU first, falling back to the mirrored CPU implementation",
            "seed": "random seed for dissolve-family modes",
        },
        notes=["Inputs must match in size; alpha handling follows the mode's semantics when channel counts differ.",
               "The HSL family (hue/saturation/color/luminosity) follows Photoshop transfer functions."],
    ),

    # ---------- Face detection ----------
    "face_detect": dict(
        title="face_detect · Face detection",
        summary="Face box detection with optional 478-point landmarks; MediaPipe/MNN backends with auto fallback.",
        desc="Detects faces in the input image and outputs boxes; enabling `output_landmarks` adds "
             "478-point landmarks (mediapipe_478 scheme, unified across backends). With `backend` "
             "= auto, MediaPipe runs first and MNN takes over when unavailable; the backend "
             "actually used is observable in the result.",
        params={
            "model_path": "MNN detector model path (empty = built-in search path)",
            "landmark_model_path": "landmark model path (used with output_landmarks)",
            "max_faces": "maximum faces kept",
            "score_threshold": "confidence threshold",
            "nms_threshold": "NMS overlap threshold",
            "output_landmarks": "emit 478-point landmarks",
        },
        notes=["Missing model assets fail the task with a pointer to the download script — never silent empty results.",
               "TG_FACE_DEBUG=1 enables backend diagnostics."],
    ),

    # ---------- Matting ----------
    "matting": dict(
        title="matting · Portrait matting",
        summary="Portrait alpha estimation with three outputs (result / gray mask / transparent cutout); MediaPipe/MNN backends.",
        desc="Estimates the portrait alpha channel with three output ports: `out` is the "
             "structured result (alpha + backend info), `mask` the 0-255 gray mask, and `cutout` "
             "the RGBA transparent cutout (alpha passthrough, background transparent). Alpha is "
             "bilinearly rescaled back to the input size. `backend` semantics match face_detect: "
             "MediaPipe (selfie segmenter) first, MNN (MODNet) fallback.",
        notes=["`cutout` feeds `gpu_alpha_composite` directly (in) over a background (in2) for final compositing.",
               "Both backends agree on foreground coverage for standard portraits (within ~0.1%).",
               "TG_MATTING_DEBUG=1 enables MNN diagnostics."],
    ),
}
