#pragma once

// MediaPipe Vision 任务层（直接编译进 libtask_graph；非子模块插件）。
//
//   mp_face_landmarker / mp_hand_landmarker / mp_pose_landmarker
//   mp_object_detector / mp_face_detector
//   mp_image_classifier / mp_image_embedder / mp_image_segmenter
//   mp_gesture_recognizer / mp_holistic_landmarker
//
// 任务层只做参数解析 + 模型路径解析，推理全部委托公共引擎 API
// <task_graph/mediapipe/vision_engine.hpp>（后续子模块同路径复用）。
// 构建开关 TASK_GRAPH_ENABLE_MEDIAPIPE：未找到预构建库时任务照常注册，
// execute 返回 FAILED 并给出可读错误（图仍可反序列化/编辑）。

#include <memory>
#include <string>
#include <vector>

#include <plugin_api.hpp>
#include <task_graph/mediapipe/vision_engine.hpp>
#include <task_graph/mediapipe/vision_types.hpp>

namespace task_graph {

class MpVisionTaskNode : public INode {
public:
    using INode::INode;

    std::vector<PortSpec> input_specs() const override;    // "image": Image | cv::Mat
    std::vector<PortSpec> output_specs() const override;   // "out": VisionResult
    std::vector<ParamSpec> param_specs() const override;   // 基础参数（子类扩展）

    void on_init() override;
    task_graph::TaskResult execute(TaskContext& ctx) override;

protected:
    // 子类：任务种类（决定引擎任务类型）。
    virtual MpVisionTask vision_task() const = 0;
    // 子类：从图参数填充任务专属选项（基础字段 model_path/delegate 由基类填）。
    virtual void fill_options(MpVisionOptions& opts) const = 0;

    // model_path 解析：ModelFinder（模型名 → 绝对路径）优先，未命中回退
    // _source_dir 相对路径（resolve_asset_path，图目录+两级祖先探测）。
    std::string resolve_model_path(std::string& err) const;

    std::shared_ptr<MpVisionEngine> engine_;
    std::string init_error_;
};

#define TG_DEFINE_MP_TASK(Class, TypeConst)                                   \
    const std::string& Class::type() const {                                  \
        static const std::string t(TypeConst);                                \
        return t;                                                             \
    }

class MpFaceLandmarkerTask : public MpVisionTaskNode {
public:
    using MpVisionTaskNode::MpVisionTaskNode;
    const std::string& type() const override;
    std::vector<ParamSpec> param_specs() const override;

protected:
    MpVisionTask vision_task() const override {
        return MpVisionTask::FaceLandmarker;
    }
    void fill_options(MpVisionOptions& opts) const override;
};

class MpHandLandmarkerTask : public MpVisionTaskNode {
public:
    using MpVisionTaskNode::MpVisionTaskNode;
    const std::string& type() const override;
    std::vector<ParamSpec> param_specs() const override;

protected:
    MpVisionTask vision_task() const override {
        return MpVisionTask::HandLandmarker;
    }
    void fill_options(MpVisionOptions& opts) const override;
};

class MpPoseLandmarkerTask : public MpVisionTaskNode {
public:
    using MpVisionTaskNode::MpVisionTaskNode;
    const std::string& type() const override;
    std::vector<ParamSpec> param_specs() const override;

protected:
    MpVisionTask vision_task() const override {
        return MpVisionTask::PoseLandmarker;
    }
    void fill_options(MpVisionOptions& opts) const override;
};

class MpObjectDetectorTask : public MpVisionTaskNode {
public:
    using MpVisionTaskNode::MpVisionTaskNode;
    const std::string& type() const override;
    std::vector<ParamSpec> param_specs() const override;

protected:
    MpVisionTask vision_task() const override {
        return MpVisionTask::ObjectDetector;
    }
    void fill_options(MpVisionOptions& opts) const override;
};

class MpFaceDetectorTask : public MpVisionTaskNode {
public:
    using MpVisionTaskNode::MpVisionTaskNode;
    const std::string& type() const override;
    std::vector<ParamSpec> param_specs() const override;

protected:
    MpVisionTask vision_task() const override {
        return MpVisionTask::FaceDetector;
    }
    void fill_options(MpVisionOptions& opts) const override;
};

class MpImageClassifierTask : public MpVisionTaskNode {
public:
    using MpVisionTaskNode::MpVisionTaskNode;
    const std::string& type() const override;
    std::vector<ParamSpec> param_specs() const override;

protected:
    MpVisionTask vision_task() const override {
        return MpVisionTask::ImageClassifier;
    }
    void fill_options(MpVisionOptions& opts) const override;
};

class MpImageEmbedderTask : public MpVisionTaskNode {
public:
    using MpVisionTaskNode::MpVisionTaskNode;
    const std::string& type() const override;
    std::vector<ParamSpec> param_specs() const override;

protected:
    MpVisionTask vision_task() const override {
        return MpVisionTask::ImageEmbedder;
    }
    void fill_options(MpVisionOptions& opts) const override;
};

class MpImageSegmenterTask : public MpVisionTaskNode {
public:
    using MpVisionTaskNode::MpVisionTaskNode;
    const std::string& type() const override;
    std::vector<ParamSpec> param_specs() const override;

protected:
    MpVisionTask vision_task() const override {
        return MpVisionTask::ImageSegmenter;
    }
    void fill_options(MpVisionOptions& opts) const override;
};

class MpGestureRecognizerTask : public MpVisionTaskNode {
public:
    using MpVisionTaskNode::MpVisionTaskNode;
    const std::string& type() const override;
    std::vector<ParamSpec> param_specs() const override;

protected:
    MpVisionTask vision_task() const override {
        return MpVisionTask::GestureRecognizer;
    }
    void fill_options(MpVisionOptions& opts) const override;
};

class MpHolisticLandmarkerTask : public MpVisionTaskNode {
public:
    using MpVisionTaskNode::MpVisionTaskNode;
    const std::string& type() const override;
    std::vector<ParamSpec> param_specs() const override;

protected:
    MpVisionTask vision_task() const override {
        return MpVisionTask::HolisticLandmarker;
    }
    void fill_options(MpVisionOptions& opts) const override;
};

}  // namespace task_graph
