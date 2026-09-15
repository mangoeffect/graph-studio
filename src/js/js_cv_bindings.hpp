#pragma once

#include <quickjs.h>

namespace task_graph {

// Register the `cv` global object with OpenCV function bindings.
// Must be called after MatWrapper::registerClass.
void registerCvBindings(JSContext* ctx);

} // namespace task_graph
