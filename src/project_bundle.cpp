#include "task_graph/project_bundle.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <utility>

#include <nlohmann/json.hpp>

#include "miniz.h"
#include "task_graph/path_utils.hpp"

namespace task_graph {
namespace {

constexpr int kProjectVersion = 1;
constexpr const char* kManifestName = "manifest.json";
constexpr const char* kFormatId = "graph-studio.project";
// 解包防护：条目数与解压总量上限（zip 炸弹兜底）
constexpr long long kMaxEntries = 10000;
constexpr long long kMaxTotalBytes = 512LL * 1024 * 1024;

// 与 wasm drop/?open 预取同源的资产扩展名清单（scripts/e2e_graph_cases.py
// 对齐；此处为最全集）。改动时三处实现需同步。
const char* const kAssetExts[] = {
    ".png", ".jpg", ".jpeg", ".webp", ".bmp", ".mp4", ".avi", ".mov",
    ".js", ".json", ".cube", ".task", ".tflite", ".mnn", ".onnx",
    ".metal", ".vert", ".frag", ".wgsl",
};

char AsciiLower(char c)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool EndsWithCi(const std::string& s, const char* suffix)
{
    const size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (AsciiLower(s[s.size() - n + i]) != AsciiLower(suffix[i]))
            return false;
    }
    return true;
}

bool EndsWith(const std::string& s, const char* suffix)
{
    const size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

bool ReadFileBytes(const std::string& path, std::vector<unsigned char>* out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size < 0) return false;
    f.seekg(0, std::ios::beg);
    out->assign(static_cast<size_t>(size), 0);
    if (size > 0)
        f.read(reinterpret_cast<char*>(out->data()), size);
    return f.good() || f.eof();
}

bool WriteFileBytes(const std::string& path, const void* data, size_t size)
{
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec) return false;
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (size > 0)
        f.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    f.flush();
    return f.good();
}

std::string NowIso8601()
{
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// 图 JSON 里的相对资产引用收集（启发式见头注释）。返回去重后的引用清单，
// 相对/绝对引用都收（绝对引用由打包侧另行分流到 missing）。
// mini nlohmann 的 const operator[]/get<T> 对缺失/类型不符会抛——所有
// 访问先经 contains/is_* 闸（本模块唯一的异常点：json::parse，就地消化）。
std::vector<std::string> CollectRefs(const std::string& graph_json)
{
    std::vector<std::string> refs;
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(graph_json);
    } catch (...) {
        return refs;
    }
    if (!doc.is_object() || !doc.contains("tasks") || !doc["tasks"].is_array())
        return refs;
    for (const auto& t : doc["tasks"]) {
        if (!t.is_object()) continue;
        std::string ty;
        if (t.contains("type") && t["type"].is_string())
            ty = t["type"].get<std::string>();
        const bool writer = EndsWith(ty, "_write") || EndsWith(ty, "video_writer");
        if (!t.contains("params") || !t["params"].is_object()) continue;
        const auto params = t["params"].get<nlohmann::json::object_t>();
        for (const auto& [key, val] : params) {
            if (!val.is_string()) continue;
            const std::string v = val.get<std::string>();
            if (v.empty() || v.find("://") != std::string::npos) continue;
            bool looks_path = v.front() == '/' || v.find('/') != std::string::npos;
            if (!looks_path) {
                for (const char* ext : kAssetExts) {
                    if (EndsWithCi(v, ext)) {
                        looks_path = true;
                        break;
                    }
                }
            }
            if (!looks_path) continue;
            if (writer && (key == "file_path" || key == "out_path")) continue;
            if (std::find(refs.begin(), refs.end(), v) == refs.end())
                refs.push_back(v);
        }
    }
    return refs;
}

// 包内条目名必须是干净的相对路径：拒绝绝对路径、..、反斜杠、盘符/流语法
// （zip-slip 防护；同时用于校验 manifest.entry）。
bool IsSafeEntryName(const std::string& name)
{
    if (name.empty() || name.front() == '/'
        || name.find('\\') != std::string::npos
        || name.find(':') != std::string::npos)
        return false;
    size_t start = 0;
    while (true) {
        const size_t slash = name.find('/', start);
        const std::string part = name.substr(
            start, slash == std::string::npos ? std::string::npos : slash - start);
        if (part.empty() || part == "." || part == "..")
            return false;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return true;
}

// 从任意形态引用（posix 绝对 / Windows 盘符反斜杠 / ../ 越界）提取可作包
// 内条目名的文件基名；拿不到干净形态时回退 "asset"。
std::string RefBasename(const std::string& ref)
{
    const size_t pos = ref.find_last_of("/\\");
    std::string name = (pos == std::string::npos) ? ref : ref.substr(pos + 1);
    const size_t colon = name.find(':');
    if (colon != std::string::npos)
        name = name.substr(colon + 1);  // "C:y.png" 形态的盘符残留
    if (!IsSafeEntryName(name))
        return "asset";
    return name;
}

// assets/ 内唯一条目名：基名冲突按 _2/_3 递增（确定性，顺序=引用收集序）。
std::string UniqueAssetEntry(const std::string& basename, std::set<std::string>* used)
{
    const std::filesystem::path p(basename);
    const std::string stem = p.stem().string();
    const std::string ext = p.extension().string();
    std::string cand = "assets/" + basename;
    for (int n = 2; used->count(cand) != 0; ++n)
        cand = "assets/" + stem + "_" + std::to_string(n) + ext;
    used->insert(cand);
    return cand;
}

// 重写【包内副本】的图 JSON：把值恰好等于 remap 键的参数替换为包内条目。
// 写出型任务的 file_path/out_path 永不重写（与 CollectRefs 排除规则一致——
// 重写会让运行期输出覆盖包内资产）。无命中/解析失败返回空串（调用方回退
// 原样字节）。
std::string RewriteGraphRefs(
    const std::string& graph_json,
    const std::vector<std::pair<std::string, std::string>>& remap)
{
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(graph_json);
    } catch (...) {
        return {};
    }
    if (!doc.is_object() || !doc.contains("tasks") || !doc["tasks"].is_array())
        return {};
    bool changed = false;
    for (auto& t : doc["tasks"]) {
        if (!t.is_object()) continue;
        std::string ty;
        if (t.contains("type") && t["type"].is_string())
            ty = t["type"].get<std::string>();
        const bool writer = EndsWith(ty, "_write") || EndsWith(ty, "video_writer");
        if (!t.contains("params") || !t["params"].is_object()) continue;
        nlohmann::json& params = t["params"];
        const auto obj = params.get<nlohmann::json::object_t>();  // 拷贝迭代，原地赋值
        for (const auto& [key, val] : obj) {
            if (!val.is_string()) continue;
            const std::string v = val.get<std::string>();
            for (const auto& [from, to] : remap) {
                if (v != from) continue;
                if (writer && (key == "file_path" || key == "out_path"))
                    continue;
                params[key] = to;
                changed = true;
            }
        }
    }
    if (!changed)
        return {};
    return doc.dump(4);
}

std::string GetStr(const nlohmann::json& obj, const char* key)
{
    if (obj.is_object() && obj.contains(key) && obj[key].is_string())
        return obj[key].get<std::string>();
    return {};
}

long long GetInt(const nlohmann::json& obj, const char* key, long long def)
{
    if (!obj.is_object() || !obj.contains(key)) return def;
    const nlohmann::json& v = obj[key];
    if (v.is_number_integer()) return v.get<long long>();
    if (v.is_number_float()) return static_cast<long long>(v.get<double>());
    return def;
}

std::string ManifestToJson(const ProjectManifest& man)
{
    nlohmann::json root;
    root["format"] = kFormatId;
    root["version"] = static_cast<long long>(man.version);
    root["entry"] = man.entry;
    root["created"] = man.created;
    root["app"] = man.app;
    nlohmann::json assets = nlohmann::json::array();
    for (const ProjectAsset& a : man.assets) {
        nlohmann::json o;
        o["path"] = a.path;
        o["size"] = a.size;
        if (!a.source.empty())
            o["source"] = a.source;  // 由该原始引用（绝对/越界）重映射而来
        assets.push_back(std::move(o));
    }
    root["assets"] = std::move(assets);
    if (!man.missing.empty()) {
        nlohmann::json arr = nlohmann::json::array();
        for (const std::string& m : man.missing)
            arr.push_back(m);
        root["missing"] = std::move(arr);
    }
    return root.dump(2);
}

bool ParseManifest(const std::string& bytes, ProjectManifest* man, std::string* error)
{
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(bytes);
    } catch (...) {
        *error = "manifest.json is not valid JSON";
        return false;
    }
    if (!root.is_object() || GetStr(root, "format") != kFormatId) {
        *error = "not a Graph Studio project (format mismatch)";
        return false;
    }
    const long long version = GetInt(root, "version", 0);
    if (version < 1 || version > kProjectVersion) {
        *error = "unsupported project version " + std::to_string(version)
                 + " (supported: " + std::to_string(kProjectVersion) + ")";
        return false;
    }
    man->version = static_cast<int>(version);
    man->entry = GetStr(root, "entry");
    man->created = GetStr(root, "created");
    man->app = GetStr(root, "app");
    if (root.contains("assets") && root["assets"].is_array()) {
        for (const auto& av : root["assets"]) {
            if (!av.is_object()) continue;
            ProjectAsset a;
            a.path = GetStr(av, "path");
            a.size = GetInt(av, "size", 0);
            a.source = GetStr(av, "source");
            man->assets.push_back(std::move(a));
        }
    }
    if (root.contains("missing") && root["missing"].is_array()) {
        for (const auto& mv : root["missing"]) {
            if (mv.is_string())
                man->missing.push_back(mv.get<std::string>());
        }
    }
    if (!IsSafeEntryName(man->entry)) {
        *error = "manifest entry is not a safe relative path: " + man->entry;
        return false;
    }
    return true;
}

struct ZipReaderGuard
{
    mz_zip_archive* z;
    ~ZipReaderGuard() { mz_zip_reader_end(z); }
};

// open_project / open_project_memory 的公共实现（包内容已在内存）。
bool OpenProjectFromMemory(const void* data, size_t size,
                           const std::string& extract_root,
                           OpenedProject* out, std::string* error)
{
    mz_zip_archive zip = {};
    if (!mz_zip_reader_init_mem(&zip, data, size, 0)) {
        *error = "not a valid project archive";
        return false;
    }
    ZipReaderGuard guard{&zip};

    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    if (count > kMaxEntries) {
        *error = "too many entries in project archive";
        return false;
    }
    long long total = 0;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st = {};
        if (!mz_zip_reader_file_stat(&zip, i, &st)) {
            *error = "corrupt entry #" + std::to_string(i);
            return false;
        }
        if (st.m_is_directory) continue;
        total += static_cast<long long>(st.m_uncomp_size);
        if (total > kMaxTotalBytes) {
            *error = "project archive too large when extracted";
            return false;
        }
        if (!IsSafeEntryName(st.m_filename)) {
            *error = std::string("unsafe entry name in archive: ") + st.m_filename;
            return false;
        }
    }

    const int man_idx = mz_zip_reader_locate_file(&zip, kManifestName, nullptr, 0);
    if (man_idx < 0) {
        *error = "archive has no manifest.json (not a Graph Studio project?)";
        return false;
    }
    size_t man_size = 0;
    void* man_data = mz_zip_reader_extract_to_heap(&zip, man_idx, &man_size, 0);
    if (!man_data) {
        *error = "failed to extract manifest.json";
        return false;
    }
    const std::string man_bytes(static_cast<const char*>(man_data), man_size);
    mz_free(man_data);
    if (!ParseManifest(man_bytes, &out->manifest, error))
        return false;

    const int entry_idx = mz_zip_reader_locate_file(
        &zip, out->manifest.entry.c_str(), nullptr, 0);
    if (entry_idx < 0) {
        *error = "manifest entry " + out->manifest.entry + " missing from archive";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(extract_root, ec);
    if (ec) {
        *error = "cannot create extract dir " + extract_root;
        return false;
    }
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st = {};
        if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory)
            continue;
        if (std::strcmp(st.m_filename, kManifestName) == 0)
            continue;  // manifest 是元数据，不落盘
        size_t sz = 0;
        void* entry = mz_zip_reader_extract_to_heap(&zip, i, &sz, 0);
        if (!entry) {
            *error = std::string("failed to extract ") + st.m_filename;
            return false;
        }
        const std::filesystem::path target =
            std::filesystem::path(extract_root) / st.m_filename;
        const bool ok = WriteFileBytes(target.string(), entry, sz);
        mz_free(entry);
        if (!ok) {
            *error = std::string("failed to write ") + st.m_filename;
            return false;
        }
    }

    out->extract_dir = extract_root;
    out->graph_path =
        (std::filesystem::path(extract_root) / out->manifest.entry).string();
    return true;
}

}  // namespace

PackReport pack_project(const std::string& graph_json_path,
                        const std::string& graph_dir,
                        const std::string& out_path,
                        const std::string& app_name)
{
    PackReport rep;
    std::vector<unsigned char> graph_bytes;
    if (!ReadFileBytes(graph_json_path, &graph_bytes)) {
        rep.error = "cannot open graph json: " + graph_json_path;
        return rep;
    }
    const std::string graph_json(graph_bytes.begin(), graph_bytes.end());
    const std::string entry =
        std::filesystem::path(graph_json_path).filename().string();

    ProjectManifest man;
    man.version = kProjectVersion;
    man.entry = entry;
    man.created = NowIso8601();
    man.app = app_name;

    struct FileEntry
    {
        std::string path;
        std::vector<unsigned char> bytes;
    };
    std::vector<FileEntry> files;
    // 包内已占条目名（图自身 + manifest + 已落位资产）：assets/ 重映射去重，
    // 也防相对引用与重映射条目同名写出重复 zip 条目。
    std::set<std::string> used_entries{kManifestName, entry};
    std::vector<std::pair<std::string, std::string>> remap;  // 原引用 -> 包内条目
    for (const std::string& ref : CollectRefs(graph_json)) {
        if (ref == entry)
            continue;  // 图自身被引用（.json 结尾的 ref），不作为资产重复打包
        if (!IsSafeEntryName(ref)) {
            // 绝对路径 / ../ 越界 / 盘符引用：能解析到已存在文件则收进包内
            // assets/（基名去重），并把包内图副本的引用重写为该包内相对路径
            // ——单文件通道（wasm ?open/拖拽/分享）因此拿得到输入图片；解
            // 析不到仍记 missing（打开端 WARN 可见）。
            const std::string resolved = resolve_asset_path(graph_dir, ref);
            std::error_code ec;
            if (std::filesystem::is_directory(resolved, ec)) {
                rep.skipped_dirs.push_back(ref);
                continue;
            }
            if (!std::filesystem::is_regular_file(resolved, ec)) {
                man.missing.push_back(ref);
                continue;
            }
            FileEntry fe;
            if (!ReadFileBytes(resolved, &fe.bytes)) {
                man.missing.push_back(ref);
                continue;
            }
            fe.path = UniqueAssetEntry(RefBasename(ref), &used_entries);
            ProjectAsset a;
            a.path = fe.path;
            a.size = static_cast<long long>(fe.bytes.size());
            a.source = ref;
            remap.emplace_back(ref, fe.path);
            files.push_back(std::move(fe));
            man.assets.push_back(std::move(a));
            continue;
        }
        // 复用核心库探测序（图目录 + 两级祖先），保证"打包进包里的文件"
        // 与"运行期会解析到的文件"一致。相对引用按原样路径落位。
        const std::string resolved = resolve_asset_path(graph_dir, ref);
        std::error_code ec;
        if (std::filesystem::is_directory(resolved, ec)) {
            rep.skipped_dirs.push_back(ref);
            continue;
        }
        if (!std::filesystem::is_regular_file(resolved, ec)) {
            man.missing.push_back(ref);
            continue;
        }
        if (used_entries.count(ref) != 0) {
            // 与已占条目名撞车（如某绝对引用的重映射条目恰好等于本相对
            // 引用）：改记 missing，避免 zip 内同名条目互相覆盖。
            man.missing.push_back(ref);
            continue;
        }
        FileEntry fe;
        fe.path = ref;
        if (!ReadFileBytes(resolved, &fe.bytes)) {
            man.missing.push_back(ref);
            continue;
        }
        ProjectAsset a;
        a.path = ref;
        a.size = static_cast<long long>(fe.bytes.size());
        files.push_back(std::move(fe));
        man.assets.push_back(std::move(a));
        used_entries.insert(ref);
    }

    // 存在重映射时，包内图副本改写为包内相对引用（无重映射保持原样字节，
    // 维系纯相对图的逐字节 round-trip 契约）。磁盘上的原 graph.json 不动。
    std::vector<unsigned char> graph_out(graph_bytes);
    if (!remap.empty()) {
        const std::string rewritten = RewriteGraphRefs(graph_json, remap);
        if (!rewritten.empty())
            graph_out.assign(rewritten.begin(), rewritten.end());
        for (const auto& [from, to] : remap) {
            (void)to;
            rep.remapped.push_back(from);
        }
    }

    mz_zip_archive zip = {};
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) {
        rep.error = "miniz writer init failed";
        return rep;
    }
    bool wok = true;
    const std::string man_bytes = ManifestToJson(man);
    wok = wok && mz_zip_writer_add_mem(&zip, kManifestName, man_bytes.data(),
                                       man_bytes.size(), MZ_DEFAULT_COMPRESSION);
    wok = wok
          && mz_zip_writer_add_mem(&zip, entry.c_str(), graph_out.data(),
                                   graph_out.size(), MZ_DEFAULT_COMPRESSION);
    for (const FileEntry& e : files) {
        wok = wok
              && mz_zip_writer_add_mem(&zip, e.path.c_str(), e.bytes.data(),
                                       e.bytes.size(), MZ_DEFAULT_COMPRESSION);
    }
    void* buf = nullptr;
    size_t buf_size = 0;
    wok = wok && mz_zip_writer_finalize_heap_archive(&zip, &buf, &buf_size);
    mz_zip_writer_end(&zip);
    if (!wok || !buf) {
        if (buf)
            mz_free(buf);
        rep.error = "failed to write project archive";
        return rep;
    }
    rep.bytes.assign(static_cast<const unsigned char*>(buf),
                     static_cast<const unsigned char*>(buf) + buf_size);
    mz_free(buf);

    if (!out_path.empty()) {
        if (!WriteFileBytes(out_path, rep.bytes.data(), rep.bytes.size())) {
            rep.error = "cannot write " + out_path;
            return rep;
        }
        rep.out_path = out_path;
    }

    rep.packed = man.assets;
    rep.missing = man.missing;
    rep.ok = true;
    return rep;
}

bool has_project_magic(const unsigned char* prefix, size_t len)
{
    if (len < 4) return false;
    if (prefix[0] != 'P' || prefix[1] != 'K') return false;
    return (prefix[2] == 3 && prefix[3] == 4) || (prefix[2] == 5 && prefix[3] == 6)
           || (prefix[2] == 7 && prefix[3] == 8);
}

bool is_project_file(const std::string& path)
{
    std::string ext = std::filesystem::path(path).extension().string();
    for (char& c : ext) c = AsciiLower(c);
    if (ext == ".tgp") return true;
    if (ext == ".json") return false;  // 明确的 json 扩展名按普通图处理，不做魔数嗅探
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    unsigned char buf[4];
    f.read(reinterpret_cast<char*>(buf), 4);
    return f.gcount() == 4 && has_project_magic(buf, 4);
}

bool open_project(const std::string& tgp_path, const std::string& extract_root,
                  OpenedProject* out, std::string* error)
{
    std::vector<unsigned char> bytes;
    if (!ReadFileBytes(tgp_path, &bytes)) {
        *error = "cannot open " + tgp_path;
        return false;
    }
    return OpenProjectFromMemory(bytes.data(), bytes.size(), extract_root, out, error);
}

bool open_project_memory(const void* data, size_t size,
                         const std::string& extract_root,
                         OpenedProject* out, std::string* error)
{
    if (!data || size == 0) {
        *error = "empty project bundle";
        return false;
    }
    return OpenProjectFromMemory(data, size, extract_root, out, error);
}

}  // namespace task_graph
