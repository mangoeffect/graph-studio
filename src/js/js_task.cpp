#include "js_task.hpp"
#ifdef TASK_GRAPH_ENABLE_OPENCV
#include "js_mat_wrapper.hpp"
#include "js_cv_bindings.hpp"
#endif
#include <task_graph/js/js_bindings.hpp>

#include <task_graph/data_types.hpp>
#include <task_graph/task_context.hpp>
#include <task_graph/path_utils.hpp>

#include <fstream>
#include <sstream>
#include <cstring>

namespace task_graph {

namespace {
const char* const kJsTaskType = "js_script";
}

// ── Static state for ctx callbacks ──
// During execute(), we store the current TaskContext so JS callbacks can access it.
// This is per-thread-safe because each JsTask instance has its own engine,
// and execute() is called from a single thread per task.
struct JsCtxState {
    TaskContext* taskCtx = nullptr;
    JsEngine* engine = nullptr;
    std::unordered_map<std::string, JSValue>* outputs = nullptr;
    std::string logBuffer;
};

static thread_local JsCtxState tl_state;

const std::string& JsTask::type() const {
    static const std::string t(kJsTaskType);
    return t;
}

// Free the cached `execute` function reference before the JsEngine (and its
// QuickJS runtime) is destroyed. Without this, JS_FreeRuntime asserts under
// DUMP_LEAKS because jsExecuteFunc_ still references a live object. Member
// destruction order guarantees engine_ is still alive when this runs.
JsTask::~JsTask() {
    if (jsCtx_ && !JS_IsNull(jsExecuteFunc_)) {
        JS_FreeValue(jsCtx_, jsExecuteFunc_);
        jsExecuteFunc_ = JS_NULL;
    }
}

std::vector<PortSpec> JsTask::input_specs() const { return inputSpecs_; }
std::vector<PortSpec> JsTask::output_specs() const { return outputSpecs_; }
std::vector<ParamSpec> JsTask::param_specs() const {
    // 注册表原型尚未 on_init（从未实例化执行）时也要暴露 script_path，
    // 否则 GUI 属性面板拿不到任何参数行——js_script 节点将无法在界面配置
    // 脚本路径（先有鸡后有蛋：script_path 只有 on_init 后才出现在 specs）。
    if (paramSpecs_.empty())
        return { make_file_param("script_path", "", "JavaScript (*.js)") };
    return paramSpecs_;
}

void JsTask::on_init() {
    const std::string raw = config().params.get_string("script_path").value_or("");
    const std::string base = config().params.get_string(kSourceDirParam).value_or("");
    scriptPath_ = resolve_asset_path(base, raw);

    if (scriptPath_.empty()) {
        // No script path configured; use minimal defaults
        paramSpecs_ = {
            make_file_param("script_path", "", "JavaScript (*.js)")
        };
        return;
    }

    engine_ = std::make_unique<JsEngine>();
    jsCtx_ = engine_->context();

#ifdef TASK_GRAPH_ENABLE_OPENCV
    MatWrapper::registerClass(jsCtx_);
    registerCvBindings(jsCtx_);
#endif

    registerConsoleBindings(jsCtx_, [this](const std::string& msg) {
        tl_state.logBuffer += msg + "\n";
    });
    registerMathBindings(jsCtx_);

    if (!loadScript()) return;
    if (!parseContract()) return;

    // Always include script_path as the first param
    ParamSpec scriptParam = make_file_param("script_path", "", "JavaScript (*.js)");
    // Prepend script_path, then append script-declared params
    std::vector<ParamSpec> allParams;
    allParams.push_back(scriptParam);
    for (auto& p : paramSpecs_) allParams.push_back(p);
    paramSpecs_ = std::move(allParams);
}

bool JsTask::loadScript() {
    try {
        engine_->evalFile(scriptPath_);
    } catch (const JsException& e) {
        tl_state.logBuffer += "[JS load error] " + std::string(e.what()) + "\n";
        return false;
    }

    // Cache the execute function reference
    JsValue fn = engine_->getGlobal("execute");
    if (!JS_IsFunction(jsCtx_, fn.get())) {
        tl_state.logBuffer += "[JS error] No 'execute' function found in script\n";
        return false;
    }
    jsExecuteFunc_ = JS_DupValue(jsCtx_, fn.get());
    return true;
}

bool JsTask::parseContract() {
    // Read inputs array
    JsValue inputsVal = engine_->getGlobal("inputs");
    if (JS_IsArray(jsCtx_, inputsVal.get())) {
        uint32_t len = 0;
        JSValue lenVal = JS_GetPropertyStr(jsCtx_, inputsVal.get(), "length");
        JS_ToUint32(jsCtx_, &len, lenVal);
        JS_FreeValue(jsCtx_, lenVal);

        for (uint32_t i = 0; i < len; i++) {
            JSValue item = JS_GetPropertyUint32(jsCtx_, inputsVal.get(), i);
            const char* name = JS_ToCString(jsCtx_, JS_GetPropertyStr(jsCtx_, item, "name"));
            JSValue reqVal = JS_GetPropertyStr(jsCtx_, item, "required");
            int required = JS_ToBool(jsCtx_, reqVal);
            JS_FreeValue(jsCtx_, reqVal);

            // type_name is empty -> no strict type checking (like existing image_filtering)
            inputSpecs_.push_back(PortSpec{name ? name : "", "", required != 0});

            if (name) JS_FreeCString(jsCtx_, name);
            JS_FreeValue(jsCtx_, item);
        }
    }

    // Read outputs array
    JsValue outputsVal = engine_->getGlobal("outputs");
    if (JS_IsArray(jsCtx_, outputsVal.get())) {
        uint32_t len = 0;
        JSValue lenVal = JS_GetPropertyStr(jsCtx_, outputsVal.get(), "length");
        JS_ToUint32(jsCtx_, &len, lenVal);
        JS_FreeValue(jsCtx_, lenVal);

        for (uint32_t i = 0; i < len; i++) {
            JSValue item = JS_GetPropertyUint32(jsCtx_, outputsVal.get(), i);
            const char* name = JS_ToCString(jsCtx_, JS_GetPropertyStr(jsCtx_, item, "name"));
            outputSpecs_.push_back(PortSpec{name ? name : "out", "", true});
            if (name) JS_FreeCString(jsCtx_, name);
            JS_FreeValue(jsCtx_, item);
        }
    }

    // Read params array
    JsValue paramsVal = engine_->getGlobal("params");
    if (JS_IsArray(jsCtx_, paramsVal.get())) {
        uint32_t len = 0;
        JSValue lenVal = JS_GetPropertyStr(jsCtx_, paramsVal.get(), "length");
        JS_ToUint32(jsCtx_, &len, lenVal);
        JS_FreeValue(jsCtx_, lenVal);

        for (uint32_t i = 0; i < len; i++) {
            JSValue item = JS_GetPropertyUint32(jsCtx_, paramsVal.get(), i);

            const char* name = JS_ToCString(jsCtx_, JS_GetPropertyStr(jsCtx_, item, "name"));
            const char* type = JS_ToCString(jsCtx_, JS_GetPropertyStr(jsCtx_, item, "type"));
            JSValue defVal = JS_GetPropertyStr(jsCtx_, item, "default");

            ParamSpec spec;
            spec.name = name ? name : "";
            if (name) JS_FreeCString(jsCtx_, name);

            if (type && std::string(type) == "int") {
                spec.type = ParamType::Int;
                int32_t dv = 0;
                JS_ToInt32(jsCtx_, &dv, defVal);
                spec.default_value = dv;
            } else if (type && std::string(type) == "float") {
                spec.type = ParamType::Float;
                double dv = 0;
                JS_ToFloat64(jsCtx_, &dv, defVal);
                spec.default_value = static_cast<float>(dv);
            } else if (type && std::string(type) == "string") {
                spec.type = ParamType::String;
                const char* dv = JS_ToCString(jsCtx_, defVal);
                spec.default_value = std::string(dv ? dv : "");
                JS_FreeCString(jsCtx_, dv);
            } else if (type && std::string(type) == "bool") {
                spec.type = ParamType::Bool;
                spec.default_value = JS_ToBool(jsCtx_, defVal) != 0;
            } else if (type && std::string(type) == "enum") {
                spec.type = ParamType::Enum;
                int32_t dv = 0;
                JS_ToInt32(jsCtx_, &dv, defVal);
                spec.default_value = dv;
                // Read enum values
                JSValue valuesArr = JS_GetPropertyStr(jsCtx_, item, "values");
                if (JS_IsArray(jsCtx_, valuesArr)) {
                    uint32_t vlen = 0;
                    JSValue vlenVal = JS_GetPropertyStr(jsCtx_, valuesArr, "length");
                    JS_ToUint32(jsCtx_, &vlen, vlenVal);
                    JS_FreeValue(jsCtx_, vlenVal);
                    for (uint32_t j = 0; j < vlen; j++) {
                        JSValue vItem = JS_GetPropertyUint32(jsCtx_, valuesArr, j);
                        const char* label = JS_ToCString(jsCtx_, JS_GetPropertyStr(jsCtx_, vItem, "label"));
                        JSValue valVal = JS_GetPropertyStr(jsCtx_, vItem, "value");
                        int32_t iv = 0;
                        JS_ToInt32(jsCtx_, &iv, valVal);
                        spec.enum_values.push_back({label ? label : "", iv});
                        if (label) JS_FreeCString(jsCtx_, label);
                        JS_FreeValue(jsCtx_, valVal);
                        JS_FreeValue(jsCtx_, vItem);
                    }
                }
                JS_FreeValue(jsCtx_, valuesArr);
            }

            // min/max/step
            JSValue minVal = JS_GetPropertyStr(jsCtx_, item, "min");
            if (!JS_IsUndefined(minVal)) {
                double d; JS_ToFloat64(jsCtx_, &d, minVal); spec.min_value = d;
            }
            JS_FreeValue(jsCtx_, minVal);

            JSValue maxVal = JS_GetPropertyStr(jsCtx_, item, "max");
            if (!JS_IsUndefined(maxVal)) {
                double d; JS_ToFloat64(jsCtx_, &d, maxVal); spec.max_value = d;
            }
            JS_FreeValue(jsCtx_, maxVal);

            JSValue stepVal = JS_GetPropertyStr(jsCtx_, item, "step");
            if (!JS_IsUndefined(stepVal)) {
                double d; JS_ToFloat64(jsCtx_, &d, stepVal); spec.step = d;
            }
            JS_FreeValue(jsCtx_, stepVal);

            paramSpecs_.push_back(spec);

            if (type) JS_FreeCString(jsCtx_, type);
            JS_FreeValue(jsCtx_, defVal);
            JS_FreeValue(jsCtx_, item);
        }
    }

    return true;
}

// ── ctx object callbacks ──

JSValue JsTask::js_ctx_input(JSContext* ctx, JSValueConst this_val,
                              int argc, JSValueConst* argv) {
    if (argc < 1 || !tl_state.taskCtx) return JS_UNDEFINED;
    const char* port = JS_ToCString(ctx, argv[0]);
    if (!port) return JS_UNDEFINED;
    std::string portName(port);
    JS_FreeCString(ctx, port);

    auto* taskCtx = tl_state.taskCtx;

#ifdef TASK_GRAPH_ENABLE_OPENCV
    // Try cv::Mat first (most common image type)
    if (auto matOpt = taskCtx->input<cv::Mat>(portName)) {
        return MatWrapper::create(ctx, *matOpt);
    }
    // Try Image type (framework's built-in image container)
    if (auto imgOpt = taskCtx->input<Image>(portName)) {
        return MatWrapper::create(ctx, imgOpt->to_mat());
    }
#endif

    // Try primitive types via get_result_value or value-based input
    // The input<T>() template handles type checking, so we try each type
    if (auto v = taskCtx->input<int>(portName)) {
        return JS_NewInt32(ctx, *v);
    }
    if (auto v = taskCtx->input<float>(portName)) {
        return JS_NewFloat64(ctx, static_cast<double>(*v));
    }
    if (auto v = taskCtx->input<double>(portName)) {
        return JS_NewFloat64(ctx, *v);
    }
    if (auto v = taskCtx->input<std::string>(portName)) {
        return JS_NewString(ctx, v->c_str());
    }
    if (auto v = taskCtx->input<bool>(portName)) {
        return JS_NewBool(ctx, *v);
    }

    return JS_NULL;
}

JSValue JsTask::js_ctx_param(JSContext* ctx, JSValueConst this_val,
                              int argc, JSValueConst* argv) {
    if (argc < 1 || !tl_state.taskCtx) return JS_UNDEFINED;
    const char* key = JS_ToCString(ctx, argv[0]);
    if (!key) return JS_UNDEFINED;

    auto& params = tl_state.taskCtx->params();

    // Try each type
    if (auto v = params.get_int(key)) {
        JSValue r = JS_NewInt32(ctx, *v);
        JS_FreeCString(ctx, key);
        return r;
    }
    if (auto v = params.get_float(key)) {
        JSValue r = JS_NewFloat64(ctx, *v);
        JS_FreeCString(ctx, key);
        return r;
    }
    if (auto v = params.get_string(key)) {
        JSValue r = JS_NewString(ctx, v->c_str());
        JS_FreeCString(ctx, key);
        return r;
    }
    if (auto v = params.get_bool(key)) {
        JSValue r = JS_NewBool(ctx, *v);
        JS_FreeCString(ctx, key);
        return r;
    }

    JS_FreeCString(ctx, key);
    return JS_UNDEFINED;
}

JSValue JsTask::js_ctx_setOutput(JSContext* ctx, JSValueConst this_val,
                                  int argc, JSValueConst* argv) {
    if (argc < 2 || !tl_state.outputs) return JS_UNDEFINED;
    const char* port = JS_ToCString(ctx, argv[0]);
    if (!port) return JS_UNDEFINED;

    // Store the JS value (dup it so it survives after the call)
    (*tl_state.outputs)[port] = JS_DupValue(ctx, argv[1]);

    JS_FreeCString(ctx, port);
    return JS_UNDEFINED;
}

JSValue JsTask::js_ctx_log(JSContext* ctx, JSValueConst this_val,
                            int argc, JSValueConst* argv) {
    for (int i = 0; i < argc; i++) {
        const char* str = JS_ToCString(ctx, argv[i]);
        if (str) {
            tl_state.logBuffer += str;
            tl_state.logBuffer += "\n";
            JS_FreeCString(ctx, str);
        }
    }
    return JS_UNDEFINED;
}

JSValue JsTask::createJsContext(TaskContext& ctx) {
    JSValue jsCtx = JS_NewObject(jsCtx_);

    JS_SetPropertyStr(jsCtx_, jsCtx, "input",
        JS_NewCFunction(jsCtx_, js_ctx_input, "input", 1));
    JS_SetPropertyStr(jsCtx_, jsCtx, "param",
        JS_NewCFunction(jsCtx_, js_ctx_param, "param", 1));
    JS_SetPropertyStr(jsCtx_, jsCtx, "setOutput",
        JS_NewCFunction(jsCtx_, js_ctx_setOutput, "setOutput", 2));
    JS_SetPropertyStr(jsCtx_, jsCtx, "log",
        JS_NewCFunction(jsCtx_, js_ctx_log, "log", 1));

    return jsCtx;
}

TaskResult JsTask::execute(TaskContext& ctx) {
    if (!engine_ || JS_IsNull(jsExecuteFunc_)) {
        return TaskResult{.status = TaskStatus::FAILED};
    }

    // Set up thread-local state for callbacks
    tl_state.taskCtx = &ctx;
    tl_state.engine = engine_.get();
    tl_state.outputs = &jsOutputs_;
    tl_state.logBuffer.clear();

    // Clear previous outputs
    for (auto& [k, v] : jsOutputs_) {
        JS_FreeValue(jsCtx_, v);
    }
    jsOutputs_.clear();

    // Create ctx object and call execute(ctx)
    JSValue jsCtxObj = createJsContext(ctx);
    JSValue global = JS_GetGlobalObject(jsCtx_);
    JSValue result = JS_Call(jsCtx_, jsExecuteFunc_, global, 1, &jsCtxObj);
    JS_FreeValue(jsCtx_, global);
    JS_FreeValue(jsCtx_, jsCtxObj);

    TaskResult ret;

    if (JS_IsException(result)) {
        JS_FreeValue(jsCtx_, result);
        std::string err = engine_->getLastError();
        ret.status = TaskStatus::FAILED;
        ret.exception = std::make_exception_ptr(std::runtime_error("JS: " + err));
    } else {
        JS_FreeValue(jsCtx_, result);
        ret = collectOutputs(ctx);
    }

    // Clean up output JS values
    for (auto& [k, v] : jsOutputs_) {
        JS_FreeValue(jsCtx_, v);
    }
    jsOutputs_.clear();

    tl_state.taskCtx = nullptr;
    tl_state.outputs = nullptr;

    return ret;
}

TaskResult JsTask::collectOutputs(TaskContext& ctx) {
    TaskResult ret;
    ret.status = TaskStatus::COMPLETED;

    if (jsOutputs_.empty()) {
        return ret;
    }

    if (jsOutputs_.size() == 1) {
        // Single output -> use .value
        auto& [port, jsVal] = *jsOutputs_.begin();

#ifdef TASK_GRAPH_ENABLE_OPENCV
        // Try to extract Mat
        auto* mw = MatWrapper::unwrap(jsCtx_, jsVal);
        if (mw) {
            ret.value = *mw;
            return ret;
        }
#endif
        // Try primitive types
        if (JS_IsNumber(jsVal)) {
            double d;
            JS_ToFloat64(jsCtx_, &d, jsVal);
            ret.value = static_cast<float>(d);
            return ret;
        }
        if (JS_IsString(jsVal)) {
            const char* s = JS_ToCString(jsCtx_, jsVal);
            ret.value = std::string(s ? s : "");
            JS_FreeCString(jsCtx_, s);
            return ret;
        }
        if (JS_IsBool(jsVal)) {
            ret.value = JS_ToBool(jsCtx_, jsVal) != 0;
            return ret;
        }
    }

    // Multiple outputs -> use .outputs map
    for (auto& [port, jsVal] : jsOutputs_) {
#ifdef TASK_GRAPH_ENABLE_OPENCV
        auto* mw = MatWrapper::unwrap(jsCtx_, jsVal);
        if (mw) {
            ret.outputs[port] = *mw;
            continue;
        }
#endif
        if (JS_IsNumber(jsVal)) {
            double d;
            JS_ToFloat64(jsCtx_, &d, jsVal);
            ret.outputs[port] = static_cast<float>(d);
        } else if (JS_IsString(jsVal)) {
            const char* s = JS_ToCString(jsCtx_, jsVal);
            ret.outputs[port] = std::string(s ? s : "");
            JS_FreeCString(jsCtx_, s);
        } else if (JS_IsBool(jsVal)) {
            ret.outputs[port] = JS_ToBool(jsCtx_, jsVal) != 0;
        }
    }

    return ret;
}

} // namespace task_graph
