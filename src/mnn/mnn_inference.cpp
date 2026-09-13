#include <task_graph/mnn/mnn_inference_task.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>

#include <plugin_api.hpp>
#include <task_graph/data_types.hpp>
#include <task_graph/path_utils.hpp>

#include <task_graph/mnn/mnn_engine.hpp>

#ifdef TASK_GRAPH_ENABLE_OPENCV
#include <opencv2/opencv.hpp>
#endif

namespace task_graph {

namespace {

// ---- 参数键（与 param_specs() 声明一一对应）----
constexpr const char* kModelPath = "model_path";
constexpr const char* kDevice = "device";
constexpr const char* kThreads = "threads";
constexpr const char* kPrecision = "precision";
constexpr const char* kInputWidth = "input_width";
constexpr const char* kInputHeight = "input_height";
constexpr const char* kMean = "mean";          // CSV "0.485,0.456,0.406"
constexpr const char* kScale = "std";          // CSV，同 mean（归一化除数）
constexpr const char* kSourceFormat = "source_format";
constexpr const char* kLabelsPath = "labels_path";
constexpr const char* kTopK = "top_k";
constexpr const char* kSoftmax = "softmax";

// source_format 枚举值（与 param_specs() 的 enum_values 对齐）
constexpr int kFmtAuto = 0;
constexpr int kFmtBgr = 1;
constexpr int kFmtRgb = 2;
constexpr int kFmtRgba = 3;
constexpr int kFmtGray = 4;

std::vector<float> parse_csv3(const std::string& s, float fallback) {
    std::vector<float> out(3, fallback);
    std::istringstream iss(s);
    std::string item;
    int i = 0;
    while (std::getline(iss, item, ',') && i < 3) {
        try {
            out[i] = std::stof(item);
        } catch (...) {
            // 解析失败保持 fallback（-fno-exceptions 构建不抛跨边界异常）
            out[i] = fallback;
        }
        ++i;
    }
    return out;
}

MnnSourceFormat resolve_source_format(const Image& img, int param_fmt) {
    // 显式参数优先；auto 按通道数推断（OpenCV 系缺省 BGR/BGRA 约定）
    switch (param_fmt) {
        case kFmtBgr:  return MnnSourceFormat::BGR;
        case kFmtRgb:  return MnnSourceFormat::RGB;
        case kFmtRgba: return MnnSourceFormat::RGBA;
        case kFmtGray: return MnnSourceFormat::GRAY;
        default:
            if (img.channels == 1) return MnnSourceFormat::GRAY;
            if (img.channels == 4) return MnnSourceFormat::RGBA;
            return MnnSourceFormat::BGR;
    }
}

bool read_labels(const std::string& path, std::vector<std::string>& labels) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        labels.push_back(line);
    }
    return !labels.empty();
}

void softmax_inplace(std::vector<float>& v) {
    if (v.empty()) return;
    const float max_v = *std::max_element(v.begin(), v.end());
    double sum = 0.0;
    for (auto& x : v) {
        x = std::exp(x - max_v);
        sum += x;
    }
    if (sum > 0.0) {
        for (auto& x : v) x = static_cast<float>(x / sum);
    }
}

TaskResult make_failed() {
    TaskResult r;
    r.status = TaskStatus::FAILED;
    return r;
}

}  // namespace

// ====================== MnnImageTaskBase ======================

MnnImageTaskBase::MnnImageTaskBase(const std::string& id, const TaskConfig& cfg)
    : INode(id, cfg) {}

std::vector<PortSpec> MnnImageTaskBase::input_specs() const {
    // 接受 task_graph::Image 或 cv::Mat（类型名留空 = 仅校验端口存在）
    return {PortSpec{"in", "", true}};
}

std::vector<PortSpec> MnnImageTaskBase::output_specs() const {
    return {PortSpec{"out", "", false}};
}

std::vector<ParamSpec> MnnImageTaskBase::param_specs() const {
    return {
        make_file_param(kModelPath, "", "MNN Model (*.mnn)"),
        make_enum_param(kDevice, 2, {{"CPU", 0}, {"Metal", 1}, {"Auto", 2}}),
        make_int_param(kThreads, 4, 1.0, 16.0),
        make_enum_param(kPrecision, 0, {{"Normal", 0}, {"High", 1}, {"Low", 2}}),
        make_int_param(kInputWidth, 0, 0.0, 8192.0),
        make_int_param(kInputHeight, 0, 0.0, 8192.0),
        make_string_param(kMean, "0,0,0"),
        make_string_param(kScale, "1,1,1"),
        make_enum_param(kSourceFormat, kFmtAuto,
                        {{"Auto", kFmtAuto}, {"BGR", kFmtBgr},
                         {"RGB", kFmtRgb}, {"RGBA", kFmtRgba},
                         {"Gray", kFmtGray}}),
    };
}

bool MnnImageTaskBase::init_engine(std::string& err) {
    MnnEngineOptions opts;
    opts.model_path = find_model(config().params.get_string(kModelPath).value_or(""));
    if (opts.model_path.empty()) {
        // ModelFinder 未命中：回退 _source_dir 资产解析（祖先探测，框架钉死的顺序）
        opts.model_path = resolve_asset_path(
            config().params.get_string(kSourceDirParam).value_or(""),
            config().params.get_string(kModelPath).value_or(""));
    }
    if (opts.model_path.empty()) {
        err = "model_path is empty";
        return false;
    }

    const int device = config().params.get_int(kDevice).value_or(2);
    opts.device = (device == 0) ? "cpu" : (device == 1 ? "metal" : "auto");
    opts.threads = config().params.get_int(kThreads).value_or(4);
    const int precision = config().params.get_int(kPrecision).value_or(0);
    opts.precision = (precision == 1) ? "high" : (precision == 2 ? "low" : "normal");
    opts.input_width = config().params.get_int(kInputWidth).value_or(0);
    opts.input_height = config().params.get_int(kInputHeight).value_or(0);

    const auto mean = parse_csv3(config().params.get_string(kMean).value_or(""), 0.f);
    const auto scale = parse_csv3(config().params.get_string(kScale).value_or(""), 1.f);
    for (int i = 0; i < 3; ++i) {
        opts.mean[i] = mean[i];
        opts.scale[i] = scale[i];
    }

    engine_ = MnnEngine::create(opts, err);
    return engine_ != nullptr;
}

bool MnnImageTaskBase::run_image(TaskContext& ctx,
                                 std::vector<MnnTensorData>& tensors,
                                 std::string& err) {
    Image img;
    if (auto img_opt = ctx.input<Image>("in")) {
        img = std::move(*img_opt);
    } else {
#ifdef TASK_GRAPH_ENABLE_OPENCV
        if (auto mat_opt = ctx.input<cv::Mat>("in")) {
            img = Image::from_mat(*mat_opt);
        }
#endif
    }

    if (!img.valid() || !img.data) {
        err = "missing/invalid image input on port 'in' (Image or cv::Mat, uint8)";
        return false;
    }
    img.ensure_cpu();
    if (img.data_type != DataType::UINT8) {
        err = "unsupported image dtype (only UINT8 supported in this version)";
        return false;
    }
    if (!engine_) {
        // on_init 未成功（或缺引擎构建）：补一次并带上原有错误信息
        std::string init_err;
        if (!init_engine(init_err)) {
            err = init_err.empty() ? "MNN engine not initialized" : init_err;
            return false;
        }
    }

    const int fmt = config().params.get_int(kSourceFormat).value_or(kFmtAuto);
    return engine_->run(img.ptr(), img.width, img.height,
                        resolve_source_format(img, fmt), tensors, err);
}

// ====================== MnnInferenceTask ======================

void MnnInferenceTask::on_init() {
    // 失败不致命：execute 里 run_image 会补一次初始化并上报可读错误
    std::string err;
    init_engine(err);
}

TaskResult MnnInferenceTask::execute(TaskContext& ctx) {
    std::vector<MnnTensorData> tensors;
    std::string err;
    if (!run_image(ctx, tensors, err)) {
        ctx.error("mnn_inference: " + err);
        return make_failed();
    }
    MnnInferenceResult result;
    result.tensors = std::move(tensors);
    TaskResult r;
    r.status = TaskStatus::COMPLETED;
    r.value = std::move(result);
    return r;
}

// ====================== MnnImageClassifierTask ======================

std::vector<ParamSpec> MnnImageClassifierTask::param_specs() const {
    auto specs = MnnImageTaskBase::param_specs();
    specs.push_back(make_file_param(kLabelsPath, "", "Labels (*.txt)"));
    specs.push_back(make_int_param(kTopK, 5, 1.0, 100.0));
    specs.push_back(make_bool_param(kSoftmax, false));
    return specs;
}

void MnnImageClassifierTask::on_init() {
    top_k_ = config().params.get_int(kTopK).value_or(5);
    softmax_ = config().params.get_bool(kSoftmax).value_or(false);

    labels_path_ = config().params.get_string(kLabelsPath).value_or("");
    if (!labels_path_.empty()) {
        const std::string found = find_model(labels_path_);
        labels_path_ = !found.empty()
                           ? found
                           : resolve_asset_path(
                                 config().params.get_string(kSourceDirParam).value_or(""),
                                 labels_path_);
    }

    std::string err;
    init_engine(err);  // 失败不致命：execute 里 run_image 会补一次并上报
}

TaskResult MnnImageClassifierTask::execute(TaskContext& ctx) {
    std::vector<MnnTensorData> tensors;
    std::string err;
    if (!run_image(ctx, tensors, err)) {
        ctx.error("mnn_image_classifier: " + err);
        return make_failed();
    }
    if (tensors.empty()) {
        ctx.error("mnn_image_classifier: model produced no output tensors");
        return make_failed();
    }

    // 分类取首个输出张量；多输出模型请改用 mnn_inference 自行处理
    std::vector<float> scores = tensors.front().data;
    if (scores.empty()) {
        ctx.error("mnn_image_classifier: first output tensor is empty");
        return make_failed();
    }

    std::vector<std::string> labels;
    if (!labels_path_.empty() && !read_labels(labels_path_, labels)) {
        ctx.warn("mnn_image_classifier: cannot read labels file '" + labels_path_ +
                 "', using class_<index>");
        labels.clear();
    }

    if (softmax_) softmax_inplace(scores);

    std::vector<int> order(scores.size());
    for (int i = 0; i < static_cast<int>(order.size()); ++i) order[i] = i;
    const int k = std::clamp<int>(top_k_, 1, static_cast<int>(order.size()));
    std::partial_sort(order.begin(), order.begin() + k, order.end(),
                      [&scores](int a, int b) { return scores[a] > scores[b]; });

    MnnClassificationResult result;
    result.categories.reserve(k);
    for (int i = 0; i < k; ++i) {
        MnnCategory c;
        c.index = order[i];
        c.score = scores[order[i]];
        c.label = (c.index >= 0 && c.index < static_cast<int>(labels.size()))
                      ? labels[c.index]
                      : "class_" + std::to_string(c.index);
        result.categories.push_back(std::move(c));
    }

    TaskResult r;
    r.status = TaskStatus::COMPLETED;
    r.value = std::move(result);
    return r;
}

}  // namespace task_graph
