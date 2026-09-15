#pragma once

#include <quickjs.h>
#include <opencv2/core.hpp>
#include <string>

namespace task_graph {

// MatWrapper: bridges cv::Mat between C++ and JavaScript.
//
// In JS, a Mat object has these properties:
//   mat.width     (int, readonly)  -> cols
//   mat.height    (int, readonly)  -> rows
//   mat.channels  (int, readonly)  -> channels()
//   mat.data      (Uint8Array, ro) -> raw pixel data (copy)
//   mat.clone()   -> deep copy Mat
//
// The C++ side stores the cv::Mat in JS_GetOpaque pointer.
class MatWrapper {
public:
    static void registerClass(JSContext* ctx);

    // Create a JS Mat object from a cv::Mat (increments refcount on mat).
    static JSValue create(JSContext* ctx, const cv::Mat& mat);

    // Extract cv::Mat from a JS Mat object. Returns nullptr if not a Mat.
    static cv::Mat* unwrap(JSContext* ctx, JSValueConst val);

private:
    cv::Mat mat_;

    MatWrapper() = default;
    explicit MatWrapper(const cv::Mat& m) : mat_(m) {}

    // JS prototype callbacks
    static JSClassID class_id_;
    static JSClassDef class_def_;
    static JSCFunctionListEntry proto_funcs[];

    static void finalizer(JSRuntime* rt, JSValue val);
    static JSValue js_get_width(JSContext* ctx, JSValueConst this_val);
    static JSValue js_get_height(JSContext* ctx, JSValueConst this_val);
    static JSValue js_get_channels(JSContext* ctx, JSValueConst this_val);
    static JSValue js_get_data(JSContext* ctx, JSValueConst this_val);
    static JSValue js_clone(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
};

} // namespace task_graph
