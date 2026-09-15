#include <task_graph/js/js_engine.hpp>

#include <quickjs-libc.h>
#include <fstream>
#include <sstream>

namespace task_graph {

JsEngine::JsEngine() {
    rt_ = JS_NewRuntime();
    if (!rt_) throw JsException("Failed to create QuickJS runtime");

    // Set memory limit (256MB) and stack size (8MB)
    JS_SetMemoryLimit(rt_, 256 * 1024 * 1024);
    JS_SetMaxStackSize(rt_, 8 * 1024 * 1024);

    ctx_ = JS_NewContext(rt_);
    if (!ctx_) {
        JS_FreeRuntime(rt_);
        rt_ = nullptr;
        throw JsException("Failed to create QuickJS context");
    }

    // Register standard libc helpers (console, setTimeout, etc.) on native.
    // quickjs-libc.c is not compiled for MSVC (see CMakeLists), so the helpers
    // are unavailable there; the plugin registers its own `console` bindings.
#if !defined(__EMSCRIPTEN__) && !defined(_MSC_VER)
    js_std_add_helpers(ctx_, 0, nullptr);
#endif

    // Register the global `console.log` -> we override below in bindings
}

JsEngine::~JsEngine() {
    if (ctx_) {
        JS_FreeContext(ctx_);
        ctx_ = nullptr;
    }
    if (rt_) {
        JS_FreeRuntime(rt_);
        rt_ = nullptr;
    }
}

JsValue JsEngine::eval(const std::string& code, const std::string& filename) {
    JSValue val = JS_Eval(ctx_, code.c_str(), code.size(), filename.c_str(),
                          JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(val)) {
        JS_FreeValue(ctx_, val);
        throw JsException(getLastError());
    }
    return JsValue(ctx_, val);
}

JsValue JsEngine::evalFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        throw JsException("Cannot open file: " + path);
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return eval(ss.str(), path);
}

JsValue JsEngine::getGlobal(const std::string& name) {
    JSValue global = JS_GetGlobalObject(ctx_);
    JSValue val = JS_GetPropertyStr(ctx_, global, name.c_str());
    JS_FreeValue(ctx_, global);
    return JsValue(ctx_, val);
}

bool JsEngine::hasFunction(const std::string& name) {
    JsValue fn = getGlobal(name);
    return JS_IsFunction(ctx_, fn.get());
}

JsValue JsEngine::callFunction(const std::string& name, int argc, JSValueConst* argv) {
    JsValue fn = getGlobal(name);
    if (!JS_IsFunction(ctx_, fn.get())) {
        throw JsException("Not a function: " + name);
    }
    JSValue global = JS_GetGlobalObject(ctx_);
    JSValue result = JS_Call(ctx_, fn.get(), global, argc, argv);
    JS_FreeValue(ctx_, global);
    if (JS_IsException(result)) {
        JS_FreeValue(ctx_, result);
        throw JsException(getLastError());
    }
    return JsValue(ctx_, result);
}

std::string JsEngine::getLastError() {
    JSValue exc = JS_GetException(ctx_);
    std::string msg;

    if (JS_IsObject(exc)) {
        JSValue message = JS_GetPropertyStr(ctx_, exc, "message");
        if (JS_IsString(message)) {
            const char* msg_str = JS_ToCString(ctx_, message);
            if (msg_str) {
                msg = msg_str;
                JS_FreeCString(ctx_, msg_str);
            }
        }
        JS_FreeValue(ctx_, message);
    }

    if (msg.empty() && JS_IsObject(exc)) {
        JSValue stack = JS_GetPropertyStr(ctx_, exc, "stack");
        const char* stack_str = JS_ToCString(ctx_, stack);
        if (stack_str) {
            msg = stack_str;
            JS_FreeCString(ctx_, stack_str);
        }
        JS_FreeValue(ctx_, stack);
    }

    if (msg.empty()) {
        const char* str = JS_ToCString(ctx_, exc);
        if (str) {
            msg = str;
            JS_FreeCString(ctx_, str);
        }
    }

    JS_FreeValue(ctx_, exc);
    return msg.empty() ? "Unknown JS error" : msg;
}

} // namespace task_graph
