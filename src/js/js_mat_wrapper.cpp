#include "js_mat_wrapper.hpp"

namespace task_graph {

JSClassID MatWrapper::class_id_ = 0;

static JSClassDef s_mat_class_def = {
    "Mat",
};

void MatWrapper::registerClass(JSContext* ctx) {
    // class_id_ is global/static: assign it once across all runtimes.
    if (class_id_ == 0) {
        JS_NewClassID(&class_id_);
        s_mat_class_def.finalizer = [](JSRuntime* rt, JSValue val) {
            auto* mw = static_cast<MatWrapper*>(JS_GetOpaque(val, class_id_));
            delete mw;
        };
    }

    // JS_NewClass must be invoked once per JSRuntime. Each JsEngine owns its own
    // runtime, so query QuickJS directly to avoid registering twice (or skipping
    // a freshly-created runtime whose address was reused by the allocator).
    JSRuntime* rt = JS_GetRuntime(ctx);
    if (!JS_IsRegisteredClass(rt, class_id_)) {
        JS_NewClass(rt, class_id_, &s_mat_class_def);
    }

    JSValue proto = JS_NewObject(ctx);

    // Width getter
    {
        JSAtom atom = JS_NewAtom(ctx, "width");
        JSValue getter = JS_NewCFunction2(ctx,
            reinterpret_cast<JSCFunction*>(+[](JSContext* c, JSValueConst tv) -> JSValue {
                auto* m = MatWrapper::unwrap(c, tv);
                return m ? JS_NewInt32(c, m->cols) : JS_EXCEPTION;
            }), "width", 0, JS_CFUNC_getter, 0);
        JS_DefinePropertyGetSet(ctx, proto, atom, getter, JS_UNDEFINED, JS_PROP_HAS_GET);
        JS_FreeAtom(ctx, atom);
    }
    // Height getter
    {
        JSAtom atom = JS_NewAtom(ctx, "height");
        JSValue getter = JS_NewCFunction2(ctx,
            reinterpret_cast<JSCFunction*>(+[](JSContext* c, JSValueConst tv) -> JSValue {
                auto* m = MatWrapper::unwrap(c, tv);
                return m ? JS_NewInt32(c, m->rows) : JS_EXCEPTION;
            }), "height", 0, JS_CFUNC_getter, 0);
        JS_DefinePropertyGetSet(ctx, proto, atom, getter, JS_UNDEFINED, JS_PROP_HAS_GET);
        JS_FreeAtom(ctx, atom);
    }
    // Channels getter
    {
        JSAtom atom = JS_NewAtom(ctx, "channels");
        JSValue getter = JS_NewCFunction2(ctx,
            reinterpret_cast<JSCFunction*>(+[](JSContext* c, JSValueConst tv) -> JSValue {
                auto* m = MatWrapper::unwrap(c, tv);
                return m ? JS_NewInt32(c, m->channels()) : JS_EXCEPTION;
            }), "channels", 0, JS_CFUNC_getter, 0);
        JS_DefinePropertyGetSet(ctx, proto, atom, getter, JS_UNDEFINED, JS_PROP_HAS_GET);
        JS_FreeAtom(ctx, atom);
    }
    // data getter -> returns Uint8Array (constructed via Uint8Array constructor)
    {
        JSAtom atom = JS_NewAtom(ctx, "data");
        JSValue getter = JS_NewCFunction2(ctx,
            reinterpret_cast<JSCFunction*>(+[](JSContext* c, JSValueConst tv) -> JSValue {
                auto* m = MatWrapper::unwrap(c, tv);
                if (!m || m->empty()) return JS_EXCEPTION;
                size_t sz = m->total() * m->elemSize();
                // Create ArrayBuffer with copied data
                JSValue abuf = JS_NewArrayBufferCopy(c, m->data, sz);
                // Construct Uint8Array from ArrayBuffer
                JSValue global = JS_GetGlobalObject(c);
                JSValue ctor = JS_GetPropertyStr(c, global, "Uint8Array");
                JS_FreeValue(c, global);
                JSValue arg = abuf;
                JSValue arr = JS_CallConstructor(c, ctor, 1, &arg);
                JS_FreeValue(c, ctor);
                JS_FreeValue(c, abuf);
                return arr;
            }), "data", 0, JS_CFUNC_getter, 0);
        JS_DefinePropertyGetSet(ctx, proto, atom, getter, JS_UNDEFINED, JS_PROP_HAS_GET);
        JS_FreeAtom(ctx, atom);
    }

    // clone() method
    JS_SetPropertyStr(ctx, proto, "clone",
        JS_NewCFunction(ctx, +[](JSContext* c, JSValueConst tv, int, JSValueConst*) -> JSValue {
            auto* m = MatWrapper::unwrap(c, tv);
            if (!m) return JS_EXCEPTION;
            return MatWrapper::create(c, m->clone());
        }, "clone", 0));

    // Set prototype for the class
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetClassProto(ctx, class_id_, proto);
    JS_FreeValue(ctx, global);
}

JSValue MatWrapper::create(JSContext* ctx, const cv::Mat& mat) {
    auto* mw = new MatWrapper(mat);
    JSValue proto = JS_GetClassProto(ctx, class_id_);
    JSValue obj = JS_NewObjectProtoClass(ctx, proto, class_id_);
    JS_FreeValue(ctx, proto);
    if (JS_IsException(obj)) {
        delete mw;
        return obj;
    }
    JS_SetOpaque(obj, mw);
    return obj;
}

cv::Mat* MatWrapper::unwrap(JSContext* ctx, JSValueConst val) {
    auto* mw = static_cast<MatWrapper*>(JS_GetOpaque2(ctx, val, class_id_));
    return mw ? &mw->mat_ : nullptr;
}

} // namespace task_graph
