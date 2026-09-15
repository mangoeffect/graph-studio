#pragma once

#include <plugin_api.hpp>
#include <string>
#include <vector>
#include <memory>

#include <task_graph/js/js_engine.hpp>

namespace task_graph {

// JsTask: A task node that executes a user-provided JavaScript script.
//
// The script declares its contract via global variables:
//   const inputs = [{ name, type, required? }]
//   const outputs = [{ name, type }]
//   const params = [{ name, type, default, min?, max?, step?, values? }]
//   function execute(ctx) { ... }
//
// The ctx object injected into JS has:
//   ctx.input("port")     -> Mat object (for image type) or primitive
//   ctx.param("key")      -> number/string/boolean
//   ctx.setOutput("port", val)  -> set output
//   ctx.log("message")    -> log to framework
class JsTask : public INode {
public:
    using INode::INode;

    ~JsTask() override;

    const std::string& type() const override;

    std::vector<PortSpec> input_specs() const override;
    std::vector<PortSpec> output_specs() const override;
    std::vector<ParamSpec> param_specs() const override;

    void on_init() override;

    TaskResult execute(TaskContext& ctx) override;

private:
    std::unique_ptr<JsEngine> engine_;
    JSValue jsExecuteFunc_ = JS_NULL;

    std::string scriptPath_;
    std::vector<PortSpec> inputSpecs_;
    std::vector<PortSpec> outputSpecs_;
    std::vector<ParamSpec> paramSpecs_;

    bool loadScript();
    bool parseContract();
    JSValue createJsContext(TaskContext& ctx);
    TaskResult collectOutputs(TaskContext& ctx);

    // Store outputs set by JS during execute
    std::unordered_map<std::string, JSValue> jsOutputs_;
    JSContext* jsCtx_ = nullptr;  // raw ptr for callbacks

    // Static callback for ctx.setOutput
    static JSValue js_ctx_setOutput(JSContext* ctx, JSValueConst this_val,
                                     int argc, JSValueConst* argv);
    static JSValue js_ctx_input(JSContext* ctx, JSValueConst this_val,
                                 int argc, JSValueConst* argv);
    static JSValue js_ctx_param(JSContext* ctx, JSValueConst this_val,
                                 int argc, JSValueConst* argv);
    static JSValue js_ctx_log(JSContext* ctx, JSValueConst this_val,
                               int argc, JSValueConst* argv);
};

} // namespace task_graph
