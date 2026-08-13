#pragma once

// Path resolution helper for the _source_dir injection mechanism in
// DAGSerializer. Workflow:
//   1. The loader (GraphStudio / test main) passes the directory containing
//      graph.json as base_dir to DAGSerializer::from_string(s, base_dir).
//   2. On deserialize, the framework injects _source_dir = base_dir into every
//      task's params.
//   3. The task reads the raw path param and calls resolve_path(base, raw) to
//      prefix relative paths with base_dir; absolute paths are returned as-is.
//
// Design goal: let graph.json reference assets (images / models / scripts) via
// relative paths so that "the whole graph folder can be moved with its asset
// directory following along".

#include <filesystem>
#include <string>

namespace task_graph {

// Framework-injected params key whose value is the absolute directory of the
// graph.json that produced the current DAG. Empty when the graph was not loaded
// from disk (programmatic DAG construction, WASM uploads, unit tests).
// Convention: any params key starting with '_' is framework-reserved and will
// NOT be serialized back to graph.json by DAGSerializer (avoids leaking host
// paths and avoids stale entries after the file moves).
inline constexpr const char* kSourceDirParam = "_source_dir";

// Resolve a possibly-relative path against base_dir:
//   - p empty             -> empty string
//   - p absolute          -> p, lexically_normal'd
//   - base_dir empty      -> p unchanged (test / WASM scenario, no prefixing)
//   - p relative + base   -> (base_dir / p), lexically_normal'd
//
// Only noexcept-equivalent std::filesystem APIs are used (is_absolute and
// lexically_normal do not throw), so this is safe under -fno-exceptions builds
// (mobile / WASM).
inline std::string resolve_path(const std::string& base_dir, const std::string& p) {
    if (p.empty()) return {};
    std::filesystem::path pp(p);
    if (pp.is_absolute() || base_dir.empty()) {
        return pp.lexically_normal().string();
    }
    return (std::filesystem::path(base_dir) / pp).lexically_normal().string();
}

}  // namespace task_graph
