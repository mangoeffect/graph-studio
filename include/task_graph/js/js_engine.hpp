#pragma once

#include <string>
#include <stdexcept>
#include <functional>

#include <quickjs.h>

namespace task_graph {

struct JsValue {
    JSContext* ctx = nullptr;
    JSValue val;

    JsValue() : ctx(nullptr), val(JS_NULL) {}
    JsValue(JSContext* c, JSValue v) : ctx(c), val(v) {}
    ~JsValue() { if (ctx) JS_FreeValue(ctx, val); }

    JsValue(const JsValue&) = delete;
    JsValue& operator=(const JsValue&) = delete;

    JsValue(JsValue&& o) noexcept : ctx(o.ctx), val(o.val) {
        o.ctx = nullptr;
    }
    JsValue& operator=(JsValue&& o) noexcept {
        if (this != &o) {
            if (ctx) JS_FreeValue(ctx, val);
            ctx = o.ctx; val = o.val;
            o.ctx = nullptr;
        }
        return *this;
    }

    JSValue get() const { return val; }
    operator JSValue() const { return val; }
    operator bool() const { return ctx != nullptr; }
};

class JsEngine {
public:
    JsEngine();
    ~JsEngine();

    JsEngine(const JsEngine&) = delete;
    JsEngine& operator=(const JsEngine&) = delete;

    JSRuntime* runtime() { return rt_; }
    JSContext* context() { return ctx_; }

    // Evaluate a script string. Returns the result value.
    // Throws JsException on error.
    JsValue eval(const std::string& code, const std::string& filename = "<script>");

    // Evaluate a file. Returns the result value.
    JsValue evalFile(const std::string& path);

    // Get a global property as a JS value.
    JsValue getGlobal(const std::string& name);

    // Check if a global function exists.
    bool hasFunction(const std::string& name);

    // Call a global function with args. Returns the result.
    JsValue callFunction(const std::string& name, int argc, JSValueConst* argv);

    // Get the last exception as a string.
    std::string getLastError();

private:
    JSRuntime* rt_ = nullptr;
    JSContext* ctx_ = nullptr;
};

class JsException : public std::runtime_error {
public:
    explicit JsException(const std::string& msg) : std::runtime_error(msg) {}
};

} // namespace task_graph
