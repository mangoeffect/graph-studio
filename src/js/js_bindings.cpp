#include <task_graph/js/js_bindings.hpp>
#include <cmath>
#include <algorithm>

namespace task_graph {

// --- console.log / console.error / console.warn ---
// The log callback is stored in a global opaque variable accessible by the C functions.

static JsLogCallback g_logCallback;

static JSValue js_console_log(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    for (int i = 0; i < argc; i++) {
        const char* str = JS_ToCString(ctx, argv[i]);
        if (str) {
            if (g_logCallback) g_logCallback(str);
            JS_FreeCString(ctx, str);
        }
    }
    return JS_UNDEFINED;
}

void registerConsoleBindings(JSContext* ctx, JsLogCallback logCb) {
    g_logCallback = std::move(logCb);

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue console = JS_NewObject(ctx);

    JSValue logFn = JS_NewCFunction(ctx, js_console_log, "log", 1);
    JS_SetPropertyStr(ctx, console, "log", logFn);
    JS_SetPropertyStr(ctx, console, "error",
        JS_NewCFunction(ctx, js_console_log, "error", 1));
    JS_SetPropertyStr(ctx, console, "warn",
        JS_NewCFunction(ctx, js_console_log, "warn", 1));
    JS_SetPropertyStr(ctx, console, "info",
        JS_NewCFunction(ctx, js_console_log, "info", 1));

    JS_SetPropertyStr(ctx, global, "console", console);
    JS_FreeValue(ctx, global);
}

// --- TG.Math utilities ---

static JSValue js_math_clamp(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_ThrowTypeError(ctx, "TG.Math.clamp(v, min, max)");
    double v, mn, mx;
    JS_ToFloat64(ctx, &v, argv[0]);
    JS_ToFloat64(ctx, &mn, argv[1]);
    JS_ToFloat64(ctx, &mx, argv[2]);
    return JS_NewFloat64(ctx, std::clamp(v, mn, mx));
}

static JSValue js_math_lerp(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_ThrowTypeError(ctx, "TG.Math.lerp(a, b, t)");
    double a, b, t;
    JS_ToFloat64(ctx, &a, argv[0]);
    JS_ToFloat64(ctx, &b, argv[1]);
    JS_ToFloat64(ctx, &t, argv[2]);
    return JS_NewFloat64(ctx, a + (b - a) * t);
}

static JSValue js_math_map(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 5) return JS_ThrowTypeError(ctx, "TG.Math.map(v, inMin, inMax, outMin, outMax)");
    double v, inMin, inMax, outMin, outMax;
    JS_ToFloat64(ctx, &v, argv[0]);
    JS_ToFloat64(ctx, &inMin, argv[1]);
    JS_ToFloat64(ctx, &inMax, argv[2]);
    JS_ToFloat64(ctx, &outMin, argv[3]);
    JS_ToFloat64(ctx, &outMax, argv[4]);
    return JS_NewFloat64(ctx, outMin + (outMax - outMin) * (v - inMin) / (inMax - inMin));
}

static JSValue js_math_degToRad(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    double deg;
    if (argc < 1 || JS_ToFloat64(ctx, &deg, argv[0])) return JS_EXCEPTION;
    return JS_NewFloat64(ctx, deg * M_PI / 180.0);
}

static JSValue js_math_radToDeg(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    double rad;
    if (argc < 1 || JS_ToFloat64(ctx, &rad, argv[0])) return JS_EXCEPTION;
    return JS_NewFloat64(ctx, rad * 180.0 / M_PI);
}

void registerMathBindings(JSContext* ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue tg = JS_NewObject(ctx);
    JSValue math = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, math, "clamp",     JS_NewCFunction(ctx, js_math_clamp, "clamp", 3));
    JS_SetPropertyStr(ctx, math, "lerp",      JS_NewCFunction(ctx, js_math_lerp, "lerp", 3));
    JS_SetPropertyStr(ctx, math, "map",       JS_NewCFunction(ctx, js_math_map, "map", 5));
    JS_SetPropertyStr(ctx, math, "degToRad",  JS_NewCFunction(ctx, js_math_degToRad, "degToRad", 1));
    JS_SetPropertyStr(ctx, math, "radToDeg",  JS_NewCFunction(ctx, js_math_radToDeg, "radToDeg", 1));

    JS_SetPropertyStr(ctx, tg, "Math", math);
    JS_SetPropertyStr(ctx, global, "TG", tg);
    JS_FreeValue(ctx, global);
}

} // namespace task_graph
