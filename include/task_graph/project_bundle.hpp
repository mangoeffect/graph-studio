#pragma once

// .tgp（task graph project）工程包：ZIP 容器（miniz 读写，源码直接编入
// libtask_graph），内含 manifest.json + 原名保留的图 JSON + 资产文件。
//
// 设计契约：相对引用的图 JSON 原样入包、资产按图内引用路径原样落位；
// 绝对路径 / 含 .. 的越界引用若能解析到已存在文件，则把文件收进包内
// assets/ 目录（基名冲突 _2/_3 递增去重），并把【包内副本】的图 JSON
// 参数值重写为该包内相对路径——用户磁盘上的原 graph.json 永不被改写；
// 写出型任务的 file_path/out_path 永不重写（重写会让运行期输出覆盖包内
// 资产）。打开端解包到会话目录（桌面 QTemporaryDir / WASM MEMFS），图
// 所在目录即解包目录，resolve_asset_path 首次探测命中——路径解析机制
// 零改动。资产发现启发式与 wasm drop/?open 预取、scripts/e2e_graph_cases.py
// 同源（路径形态 + 资产扩展名，写出型任务的 file_path/out_path 除外，
// URL 除外）。
//
// v1 边界：解析不到文件的引用（含不存在的绝对路径）记 manifest.missing，
// 打开端 WARN；目录引用（如 render 的 effects_path）不打包，仅打包报告
// 提示；打开只读，保存=重新导出新包。
//
// API 边界不抛异常：所有失败经返回值 + error 出参表达（对齐
// resolve_asset_path 的 noexcept 风格，内部实现才使用异常）。

#include <cstddef>
#include <string>
#include <vector>

namespace task_graph {

struct ProjectAsset
{
    std::string path;        // 包内相对路径（posix 分隔符）
    long long size = 0;
    // 非空 = 该资产由此原始引用（绝对路径 / 越界 ../）重映射收编而来，
    // 包内图副本的对应参数已重写为 path；空 = 相对引用原样落位。
    std::string source;
};

struct ProjectManifest
{
    int version = 0;
    std::string entry;       // 图 JSON 在包内的文件名
    std::string created;     // ISO8601
    std::string app;         // 打包端应用标识
    std::vector<ProjectAsset> assets;
    std::vector<std::string> missing;  // 打包机上未能解析成可打包文件的引用
};

struct PackReport
{
    bool ok = false;
    std::string error;
    std::string out_path;                  // 写出的 .tgp 路径（仅落盘时）
    std::vector<unsigned char> bytes;      // 包内容（内存持有，落盘与否都有）
    std::vector<ProjectAsset> packed;
    std::vector<std::string> missing;      // 未解析引用（已记入 manifest.missing）
    std::vector<std::string> skipped_dirs; // 解析到目录的引用（v1 不打包目录）
    // 已收编进包内 assets/ 的原始引用（绝对路径/越界，对应包内图副本已
    // 重写；manifest.assets[].source 记录逐条映射）。
    std::vector<std::string> remapped;
};

struct OpenedProject
{
    ProjectManifest manifest;
    std::string extract_dir;  // 解包目录（调用方管理生命周期）
    std::string graph_path;   // extract_dir + "/" + manifest.entry
};

// 打包：graph_json_path 图 JSON；graph_dir 相对引用解析基准（图所在目录，
// 祖先探测序与运行期 resolve_asset_path 一致）；out_path 非空时同时落盘；
// app_name 写入 manifest.app（默认 "task_graph"）。
PackReport pack_project(const std::string& graph_json_path,
                        const std::string& graph_dir,
                        const std::string& out_path = std::string(),
                        const std::string& app_name = "task_graph");

// 嗅探：.tgp 扩展名 → 工程；.json → 普通；其它扩展名读文件头 PK 魔数。
bool is_project_file(const std::string& path);
bool has_project_magic(const unsigned char* prefix, size_t len);

// 解包 tgp_path 到 extract_root（目录，不存在则创建），返回待加载的图路径。
// 失败时 *error 给出可读原因（zip-slip / 超限 / 版本不符等）。manifest.json
// 条目不落盘（纯元数据）。
bool open_project(const std::string& tgp_path, const std::string& extract_root,
                  OpenedProject* out, std::string* error);

// 内存版（WASM 交换文件/网络字节流场景）：与 open_project 同一套校验与
// 解包语义，只是包内容来自调用方缓冲区。
bool open_project_memory(const void* data, size_t size,
                         const std::string& extract_root,
                         OpenedProject* out, std::string* error);

}  // namespace task_graph
