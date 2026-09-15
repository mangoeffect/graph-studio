#pragma once

#include <quickjs.h>
#include <string>
#include <functional>

namespace task_graph {

// Callback type for logging from JS
using JsLogCallback = std::function<void(const std::string&)>;

// Register the `console` global object and `TG.Math` utilities.
void registerConsoleBindings(JSContext* ctx, JsLogCallback logCb);

// Register the `TG.Math` utility namespace.
void registerMathBindings(JSContext* ctx);

} // namespace task_graph
