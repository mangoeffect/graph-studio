#include "js_cv_bindings.hpp"
#include "js_mat_wrapper.hpp"

#include <opencv2/imgproc.hpp>
#include <opencv2/core.hpp>

namespace task_graph {

// Helper: extract cv::Mat from a JS value (must be a Mat wrapper)
static cv::Mat getMat(JSContext* ctx, JSValueConst val) {
    auto* m = MatWrapper::unwrap(ctx, val);
    return m ? *m : cv::Mat();
}

// Helper: create JS Mat from cv::Mat
static JSValue makeMat(JSContext* ctx, const cv::Mat& m) {
    return MatWrapper::create(ctx, m);
}

// --- Blur ---
static JSValue js_cv_blur(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_ThrowTypeError(ctx, "cv.blur(mat, kx, ky)");
    cv::Mat src = getMat(ctx, argv[0]);
    int kx, ky;
    JS_ToInt32(ctx, &kx, argv[1]);
    JS_ToInt32(ctx, &ky, argv[2]);
    cv::Mat dst;
    cv::blur(src, dst, cv::Size(kx, ky));
    return makeMat(ctx, dst);
}

// --- GaussianBlur ---
static JSValue js_cv_gaussianBlur(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_ThrowTypeError(ctx, "cv.gaussianBlur(mat, kx, ky, sigmaX)");
    cv::Mat src = getMat(ctx, argv[0]);
    int kx, ky;
    double sigma;
    JS_ToInt32(ctx, &kx, argv[1]);
    JS_ToInt32(ctx, &ky, argv[2]);
    JS_ToFloat64(ctx, &sigma, argv[3]);
    cv::Mat dst;
    cv::GaussianBlur(src, dst, cv::Size(kx, ky), sigma);
    return makeMat(ctx, dst);
}

// --- MedianBlur ---
static JSValue js_cv_medianBlur(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "cv.medianBlur(mat, ksize)");
    cv::Mat src = getMat(ctx, argv[0]);
    int ksize;
    JS_ToInt32(ctx, &ksize, argv[1]);
    cv::Mat dst;
    cv::medianBlur(src, dst, ksize);
    return makeMat(ctx, dst);
}

// --- BilateralFilter ---
static JSValue js_cv_bilateralFilter(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 4) return JS_ThrowTypeError(ctx, "cv.bilateralFilter(mat, d, sigmaColor, sigmaSpace)");
    cv::Mat src = getMat(ctx, argv[0]);
    int d;
    double sc, ss;
    JS_ToInt32(ctx, &d, argv[1]);
    JS_ToFloat64(ctx, &sc, argv[2]);
    JS_ToFloat64(ctx, &ss, argv[3]);
    cv::Mat dst;
    cv::bilateralFilter(src, dst, d, sc, ss);
    return makeMat(ctx, dst);
}

// --- Threshold ---
static JSValue js_cv_threshold(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 4) return JS_ThrowTypeError(ctx, "cv.threshold(mat, thresh, maxval, type)");
    cv::Mat src = getMat(ctx, argv[0]);
    double thresh, maxval;
    int type;
    JS_ToFloat64(ctx, &thresh, argv[1]);
    JS_ToFloat64(ctx, &maxval, argv[2]);
    JS_ToInt32(ctx, &type, argv[3]);
    cv::Mat dst;
    cv::threshold(src, dst, thresh, maxval, type);
    return makeMat(ctx, dst);
}

// --- CvtColor ---
static JSValue js_cv_cvtColor(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "cv.cvtColor(mat, code)");
    cv::Mat src = getMat(ctx, argv[0]);
    int code;
    JS_ToInt32(ctx, &code, argv[1]);
    cv::Mat dst;
    cv::cvtColor(src, dst, code);
    return makeMat(ctx, dst);
}

// --- Resize ---
static JSValue js_cv_resize(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_ThrowTypeError(ctx, "cv.resize(mat, width, height, interp?)");
    cv::Mat src = getMat(ctx, argv[0]);
    int w, h;
    JS_ToInt32(ctx, &w, argv[1]);
    JS_ToInt32(ctx, &h, argv[2]);
    int interp = cv::INTER_LINEAR;
    if (argc > 3) JS_ToInt32(ctx, &interp, argv[3]);
    cv::Mat dst;
    cv::resize(src, dst, cv::Size(w, h), 0, 0, interp);
    return makeMat(ctx, dst);
}

// --- Flip ---
static JSValue js_cv_flip(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "cv.flip(mat, flipCode)");
    cv::Mat src = getMat(ctx, argv[0]);
    int code;
    JS_ToInt32(ctx, &code, argv[1]);
    cv::Mat dst;
    cv::flip(src, dst, code);
    return makeMat(ctx, dst);
}

// --- Canny ---
static JSValue js_cv_canny(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_ThrowTypeError(ctx, "cv.Canny(mat, threshold1, threshold2)");
    cv::Mat src = getMat(ctx, argv[0]);
    double t1, t2;
    JS_ToFloat64(ctx, &t1, argv[1]);
    JS_ToFloat64(ctx, &t2, argv[2]);
    cv::Mat dst;
    cv::Canny(src, dst, t1, t2);
    return makeMat(ctx, dst);
}

// --- Sobel ---
static JSValue js_cv_sobel(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 4) return JS_ThrowTypeError(ctx, "cv.Sobel(mat, ddepth, dx, dy, ksize?)");
    cv::Mat src = getMat(ctx, argv[0]);
    int ddepth, dx, dy, ksize = 3;
    JS_ToInt32(ctx, &ddepth, argv[1]);
    JS_ToInt32(ctx, &dx, argv[2]);
    JS_ToInt32(ctx, &dy, argv[3]);
    if (argc > 4) JS_ToInt32(ctx, &ksize, argv[4]);
    cv::Mat dst;
    cv::Sobel(src, dst, ddepth, dx, dy, ksize);
    return makeMat(ctx, dst);
}

// --- Arithmetic ops ---
static JSValue js_cv_add(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "cv.add(a, b)");
    cv::Mat a = getMat(ctx, argv[0]), b = getMat(ctx, argv[1]);
    cv::Mat dst;
    cv::add(a, b, dst);
    return makeMat(ctx, dst);
}

static JSValue js_cv_subtract(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "cv.subtract(a, b)");
    cv::Mat a = getMat(ctx, argv[0]), b = getMat(ctx, argv[1]);
    cv::Mat dst;
    cv::subtract(a, b, dst);
    return makeMat(ctx, dst);
}

static JSValue js_cv_bitwiseAnd(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "cv.bitwiseAnd(a, b)");
    cv::Mat a = getMat(ctx, argv[0]), b = getMat(ctx, argv[1]);
    cv::Mat dst;
    cv::bitwise_and(a, b, dst);
    return makeMat(ctx, dst);
}

static JSValue js_cv_bitwiseOr(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "cv.bitwiseOr(a, b)");
    cv::Mat a = getMat(ctx, argv[0]), b = getMat(ctx, argv[1]);
    cv::Mat dst;
    cv::bitwise_or(a, b, dst);
    return makeMat(ctx, dst);
}

// --- Create Mat from raw data ---
static JSValue js_cv_createMat(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_ThrowTypeError(ctx, "cv.createMat(width, height, channels)");
    int w, h, ch;
    JS_ToInt32(ctx, &w, argv[0]);
    JS_ToInt32(ctx, &h, argv[1]);
    JS_ToInt32(ctx, &ch, argv[2]);
    cv::Mat mat(h, w, CV_MAKETYPE(CV_8U, ch), cv::Scalar(0));
    return makeMat(ctx, mat);
}

void registerCvBindings(JSContext* ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue cv = JS_NewObject(ctx);

    auto addFn = [&](const char* name, JSCFunction* fn, int nargs) {
        JS_SetPropertyStr(ctx, cv, name, JS_NewCFunction(ctx, fn, name, nargs));
    };

    // Functions
    addFn("blur",            js_cv_blur,           3);
    addFn("gaussianBlur",    js_cv_gaussianBlur,   4);
    addFn("medianBlur",      js_cv_medianBlur,     2);
    addFn("bilateralFilter", js_cv_bilateralFilter, 4);
    addFn("threshold",       js_cv_threshold,      4);
    addFn("cvtColor",        js_cv_cvtColor,       2);
    addFn("resize",          js_cv_resize,         4);
    addFn("flip",            js_cv_flip,           2);
    addFn("Canny",           js_cv_canny,          3);
    addFn("Sobel",           js_cv_sobel,          5);
    addFn("add",             js_cv_add,            2);
    addFn("subtract",        js_cv_subtract,       2);
    addFn("bitwiseAnd",      js_cv_bitwiseAnd,     2);
    addFn("bitwiseOr",       js_cv_bitwiseOr,      2);
    addFn("createMat",       js_cv_createMat,      3);

    // Constants
    auto addConst = [&](const char* name, int val) {
        JS_SetPropertyStr(ctx, cv, name, JS_NewInt32(ctx, val));
    };
    addConst("THRESH_BINARY",    cv::THRESH_BINARY);
    addConst("THRESH_BINARY_INV", cv::THRESH_BINARY_INV);
    addConst("THRESH_TRUNC",     cv::THRESH_TRUNC);
    addConst("THRESH_TOZERO",    cv::THRESH_TOZERO);
    addConst("THRESH_OTSU",      cv::THRESH_OTSU);
    addConst("COLOR_BGR2GRAY",   cv::COLOR_BGR2GRAY);
    addConst("COLOR_GRAY2BGR",   cv::COLOR_GRAY2BGR);
    addConst("COLOR_BGR2HSV",    cv::COLOR_BGR2HSV);
    addConst("COLOR_BGR2RGB",    cv::COLOR_BGR2RGB);
    addConst("INTER_LINEAR",     cv::INTER_LINEAR);
    addConst("INTER_NEAREST",    cv::INTER_NEAREST);
    addConst("INTER_CUBIC",      cv::INTER_CUBIC);
    addConst("CV_8U",            CV_8U);
    addConst("CV_32F",           CV_32F);

    JS_SetPropertyStr(ctx, global, "cv", cv);
    JS_FreeValue(ctx, global);
}

} // namespace task_graph
