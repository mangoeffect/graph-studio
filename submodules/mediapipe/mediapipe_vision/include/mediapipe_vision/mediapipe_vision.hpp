#pragma once

#include <memory>
#include <string>
#include <vector>

#include <plugin_api.hpp>
#include <task_graph/data_types.hpp>
#include <task_graph/task_context.hpp>

#include "mediapipe_vision/mediapipe_types.hpp"

namespace mediapipe_vision {

class MediaPipeVisionTaskBase : public task_graph::INode {
public:
    using task_graph::INode::INode;

    std::vector<task_graph::PortSpec> input_specs() const override;
    std::vector<task_graph::ParamSpec> param_specs() const override;

    void on_init() override;

protected:
    task_graph::TaskResult run_image(task_graph::TaskContext& ctx,
                                     task_graph::VisionResult& out);
    static task_graph::TaskResult failed(const std::string& msg);
};

class FaceLandmarkerTask : public MediaPipeVisionTaskBase {
public:
    using MediaPipeVisionTaskBase::MediaPipeVisionTaskBase;

    const std::string& type() const override;
    task_graph::TaskResult execute(task_graph::TaskContext& ctx) override;
    std::vector<task_graph::ParamSpec> param_specs() const override;
};

class HandLandmarkerTask : public MediaPipeVisionTaskBase {
public:
    using MediaPipeVisionTaskBase::MediaPipeVisionTaskBase;

    const std::string& type() const override;
    task_graph::TaskResult execute(task_graph::TaskContext& ctx) override;
    std::vector<task_graph::ParamSpec> param_specs() const override;
};

class PoseLandmarkerTask : public MediaPipeVisionTaskBase {
public:
    using MediaPipeVisionTaskBase::MediaPipeVisionTaskBase;

    const std::string& type() const override;
    task_graph::TaskResult execute(task_graph::TaskContext& ctx) override;
    std::vector<task_graph::ParamSpec> param_specs() const override;
};

class ObjectDetectorTask : public MediaPipeVisionTaskBase {
public:
    using MediaPipeVisionTaskBase::MediaPipeVisionTaskBase;

    const std::string& type() const override;
    task_graph::TaskResult execute(task_graph::TaskContext& ctx) override;
    std::vector<task_graph::ParamSpec> param_specs() const override;
};

}  // namespace mediapipe_vision
