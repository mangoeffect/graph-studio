// .tgp 工程包纯逻辑测试：pack→open round-trip、解包布局契约
//（resolve_asset_path 对解包目录首次探测命中——运行期读任务的资产解析
// 不经任何改写）、内存通道（open_project_memory）、zip-slip/超限拒绝、
// manifest.missing 记录、嗅探。不做真实任务执行：带真图执行的端到端由
// 桌面 --open x.tgp --run 冒烟与 WASM E2E 的 project 场景覆盖。

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "miniz.h"
#include "task_graph/path_utils.hpp"
#include "task_graph/project_bundle.hpp"

namespace fs = std::filesystem;

namespace {

// 与运行期读任务一致的参数形态：file_path 相对引用 + 一个 writer 出参
//（不应入包）+ 一个绝对路径引用（v1 不打包，记 missing）。
const char* kGraphJson = R"JSON({
  "version": "2.0",
  "tasks": [
    { "id": "src", "type": "opencv_image_read",
      "params": { "file_path": "data/test.png" } },
    { "id": "gray", "type": "opencv_cvt_color", "params": { "code": "BGR2GRAY" } },
    { "id": "out", "type": "opencv_image_write",
      "params": { "file_path": "result.png" } },
    { "id": "abs", "type": "opencv_image_read",
      "params": { "file_path": "/Users/whoever/tests/data/abs.png" } }
  ],
  "edges": [
    { "from": "src", "from_port": "out", "to": "gray", "to_port": "in" }
  ]
})JSON";

// 最小合法 PNG 头（8 字节签名 + 填充）：内容本身不参与断言，
// round-trip 只比较字节一致。
std::vector<unsigned char> FakePng(size_t extra)
{
    const unsigned char sig[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::vector<unsigned char> b(sig, sig + sizeof(sig));
    b.insert(b.end(), extra, 0);
    return b;
}

void WriteBytes(const std::string& path, const void* data, size_t size)
{
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(f.good());
    if (size) f.write(static_cast<const char*>(data), std::streamsize(size));
    ASSERT_TRUE(f.good());
}

std::vector<unsigned char> ReadBytes(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(f),
                                      std::istreambuf_iterator<char>());
}

void ReplaceAll(std::string* s, const std::string& from, const std::string& to)
{
    ASSERT_EQ(from.size(), to.size());  // 等长替换不破坏 zip 结构
    size_t pos = 0;
    while ((pos = s->find(from, pos)) != std::string::npos) {
        s->replace(pos, from.size(), to);
        pos += to.size();
    }
}

// miniz 堆 writer 构造 zip 字节（STORED/DEFLATE 由 miniz 选，名字一律经
// writer 的写侧 zip-slip 防护——恶意名须等长字节替换注入，见下）。
std::vector<unsigned char> BuildZip(
    const std::vector<std::pair<std::string, std::string>>& entries)
{
    mz_zip_archive zip = {};
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) return {};
    for (const auto& [name, content] : entries) {
        if (!mz_zip_writer_add_mem(&zip, name.c_str(), content.data(),
                                   content.size(), MZ_DEFAULT_COMPRESSION)) {
            mz_zip_writer_end(&zip);
            return {};
        }
    }
    void* buf = nullptr;
    size_t size = 0;
    const bool ok = mz_zip_writer_finalize_heap_archive(&zip, &buf, &size);
    mz_zip_writer_end(&zip);
    std::vector<unsigned char> bytes;
    if (ok && buf)
        bytes.assign(static_cast<const unsigned char*>(buf),
                     static_cast<const unsigned char*>(buf) + size);
    if (buf) mz_free(buf);
    return bytes;
}

class ProjectBundleTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        root_ = fs::temp_directory_path()
                / ("tg_project_bundle_" + std::to_string(++counter_));
        fs::create_directories(root_ / "graphs");
        fs::create_directories(root_ / "data");
        graph_path_ = (root_ / "graphs/bundle_test.json").string();
        graph_dir_ = (root_ / "graphs").string();
        WriteBytes(graph_path_, kGraphJson, std::strlen(kGraphJson));
        asset_ = FakePng(1024);
        WriteBytes((root_ / "data/test.png").string(), asset_.data(),
                   asset_.size());
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    std::string ExtractDir(const char* name) const
    {
        return (root_ / name).string();
    }

    static int counter_;
    fs::path root_;
    std::string graph_path_, graph_dir_;
    std::vector<unsigned char> asset_;
};

int ProjectBundleTest::counter_ = 0;

}  // namespace

TEST_F(ProjectBundleTest, PackOpenRoundTrip)
{
    const std::string tgp = (root_ / "bundle_test.tgp").string();
    const auto rep = task_graph::pack_project(graph_path_, graph_dir_, tgp);
    ASSERT_TRUE(rep.ok) << rep.error;
    ASSERT_TRUE(fs::exists(tgp));
    ASSERT_FALSE(rep.bytes.empty());

    // 打包内容：图 + 1 个资产（writer 出参与绝对引用不进 assets）
    ASSERT_EQ(rep.packed.size(), 1u);
    EXPECT_EQ(rep.packed[0].path, "data/test.png");
    EXPECT_EQ(rep.packed[0].size, static_cast<long long>(asset_.size()));

    const std::string ex = ExtractDir("ex");
    task_graph::OpenedProject opened;
    std::string err;
    ASSERT_TRUE(task_graph::open_project(tgp, ex, &opened, &err)) << err;
    EXPECT_EQ(opened.manifest.version, 1);
    EXPECT_EQ(opened.manifest.entry, "bundle_test.json");
    EXPECT_EQ(opened.manifest.assets.size(), 1u);

    // 解包布局契约：图在解包根、资产按引用原样落位——运行期
    // resolve_asset_path(解包目录, 引用) 首次探测即命中（零改写证明）。
    EXPECT_TRUE(fs::exists(opened.graph_path));
    const std::string resolved =
        task_graph::resolve_asset_path(opened.extract_dir, "data/test.png");
    EXPECT_EQ(resolved,
              (fs::path(opened.extract_dir) / "data/test.png").lexically_normal().string());
    EXPECT_TRUE(fs::exists(resolved));

    // 字节 round-trip：图与资产内容一致
    const auto graph_back = ReadBytes(opened.graph_path);
    EXPECT_TRUE(std::equal(graph_back.begin(), graph_back.end(), kGraphJson,
                           kGraphJson + std::strlen(kGraphJson)));
    const auto asset_back = ReadBytes(resolved);
    EXPECT_TRUE(asset_back == asset_);

    // manifest 不落盘（纯元数据）
    EXPECT_FALSE(fs::exists(fs::path(ex) / "manifest.json"));
}

TEST_F(ProjectBundleTest, OpenFromMemory)
{
    // 内存通道（WASM 交换字节流场景）与文件通道同语义
    const auto rep = task_graph::pack_project(graph_path_, graph_dir_);
    ASSERT_TRUE(rep.ok) << rep.error;

    const std::string ex = ExtractDir("ex_mem");
    task_graph::OpenedProject opened;
    std::string err;
    ASSERT_TRUE(task_graph::open_project_memory(rep.bytes.data(),
                                                rep.bytes.size(), ex, &opened,
                                                &err))
        << err;
    EXPECT_EQ(opened.manifest.entry, "bundle_test.json");
    const auto graph_back = ReadBytes(opened.graph_path);
    EXPECT_TRUE(std::equal(graph_back.begin(), graph_back.end(), kGraphJson,
                           kGraphJson + std::strlen(kGraphJson)));
    EXPECT_TRUE(fs::exists(fs::path(ex) / "data/test.png"));

    // 空输入拒绝
    task_graph::OpenedProject junk;
    EXPECT_FALSE(task_graph::open_project_memory(nullptr, 0, ExtractDir("ex0"),
                                                 &junk, &err));
    EXPECT_FALSE(err.empty());
}

TEST_F(ProjectBundleTest, Sniffing)
{
    const std::string tgp = (root_ / "x.tgp").string();
    ASSERT_TRUE(task_graph::pack_project(graph_path_, graph_dir_, tgp).ok);
    EXPECT_TRUE(task_graph::is_project_file(tgp));      // 扩展名
    EXPECT_FALSE(task_graph::is_project_file(graph_path_));  // .json 不嗅探

    const unsigned char pk34[] = {0x50, 0x4b, 0x03, 0x04};
    const unsigned char pk56[] = {0x50, 0x4b, 0x05, 0x06};
    const unsigned char notpk[] = {'{', '"', 'a', '"', ':', '1', '}'};
    EXPECT_TRUE(task_graph::has_project_magic(pk34, sizeof(pk34)));
    EXPECT_TRUE(task_graph::has_project_magic(pk56, sizeof(pk56)));
    EXPECT_FALSE(task_graph::has_project_magic(notpk, sizeof(notpk)));

    // 无扩展名文件按魔数嗅探
    const std::string no_ext = (root_ / "plain").string();
    WriteBytes(no_ext, pk34, sizeof(pk34));
    EXPECT_TRUE(task_graph::is_project_file(no_ext));
}

TEST_F(ProjectBundleTest, RejectsUnsafeEntry)
{
    // miniz 3.x 的 writer 自身会拒绝含 ../ 或绝对路径的条目名（写侧防护），
    // 所以恶意包须绕过 writer 构造：先写安全名等长的包，再对整个包做
    // 字节替换（local header 与 central directory 里的名字一并替换；CRC
    // 不覆盖文件名）。
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"xx/evil.txt", "../evil.txt"},
        {"qabs/evil.txt", "/abs/evil.txt"},
        {"a/zz/xx/evil.txt", "a/../../evil.txt"},
    };
    const std::string manifest =
        "{\"format\":\"graph-studio.project\",\"version\":1,"
        "\"entry\":\"g.json\"}";
    for (const auto& [safe_name, evil_name] : cases) {
        auto bytes = BuildZip({{"manifest.json", manifest},
                               {safe_name, "x"}});
        ASSERT_FALSE(bytes.empty());
        std::string s(bytes.begin(), bytes.end());
        ASSERT_NE(s.find(safe_name), std::string::npos);
        ReplaceAll(&s, safe_name, evil_name);

        const std::string ex = ExtractDir("ex_evil");
        std::error_code ec;
        fs::remove_all(ex, ec);
        task_graph::OpenedProject opened;
        std::string err;
        EXPECT_FALSE(task_graph::open_project_memory(s.data(), s.size(), ex,
                                                     &opened, &err))
            << evil_name;
        EXPECT_NE(err.find("unsafe"), std::string::npos) << err;
        // 解包目录外不得出现逃逸文件
        EXPECT_FALSE(fs::exists(root_ / "evil.txt"));
        EXPECT_FALSE(fs::exists(fs::path(ex) / "evil.txt"));
    }
}

TEST_F(ProjectBundleTest, RejectsBadManifest)
{
    // 一个没有 manifest.json 的普通 zip
    auto plain = BuildZip({{"hello.txt", "hello"}});
    ASSERT_FALSE(plain.empty());
    task_graph::OpenedProject opened;
    std::string err;
    EXPECT_FALSE(task_graph::open_project_memory(plain.data(), plain.size(),
                                                 ExtractDir("x"), &opened,
                                                 &err));
    EXPECT_NE(err.find("manifest"), std::string::npos) << err;

    // manifest 版本不符：构造 version 99 的包
    const std::string bad_man =
        R"({"format":"graph-studio.project","version":99,"entry":"g.json"})";
    auto bad = BuildZip({{"manifest.json", bad_man}, {"g.json", "{}"}});
    ASSERT_FALSE(bad.empty());
    EXPECT_FALSE(task_graph::open_project_memory(bad.data(), bad.size(),
                                                 ExtractDir("y"), &opened,
                                                 &err));
    EXPECT_NE(err.find("version"), std::string::npos) << err;

    // manifest entry 缺失于包内
    const std::string lost_man =
        R"({"format":"graph-studio.project","version":1,"entry":"gone.json"})";
    auto lost = BuildZip({{"manifest.json", lost_man}, {"g.json", "{}"}});
    ASSERT_FALSE(lost.empty());
    EXPECT_FALSE(task_graph::open_project_memory(lost.data(), lost.size(),
                                                 ExtractDir("z"), &opened,
                                                 &err));
    EXPECT_NE(err.find("gone.json"), std::string::npos) << err;
}

TEST_F(ProjectBundleTest, RejectsTooManyEntries)
{
    // 解包条目上限（zip 炸弹兜底）：kMaxEntries+1 个条目
    std::vector<std::pair<std::string, std::string>> entries = {
        {"manifest.json",
         R"({"format":"graph-studio.project","version":1,"entry":"g.json"})"},
        {"g.json", "{}"}};
    for (int i = 0; i < 10000; ++i)
        entries.emplace_back("d/f" + std::to_string(i) + ".txt", "x");
    auto bytes = BuildZip(entries);
    ASSERT_FALSE(bytes.empty());
    task_graph::OpenedProject opened;
    std::string err;
    EXPECT_FALSE(task_graph::open_project_memory(bytes.data(), bytes.size(),
                                                 ExtractDir("cap"), &opened,
                                                 &err));
    EXPECT_NE(err.find("too many entries"), std::string::npos) << err;
}

TEST_F(ProjectBundleTest, MissingAndSkippedRefs)
{
    // 绝对路径引用：不打包、进 missing（打开端可见）
    const std::string tgp = (root_ / "m.tgp").string();
    const auto rep = task_graph::pack_project(graph_path_, graph_dir_, tgp);
    ASSERT_TRUE(rep.ok) << rep.error;
    EXPECT_NE(std::find(rep.missing.begin(), rep.missing.end(),
                        "/Users/whoever/tests/data/abs.png"),
              rep.missing.end());
    EXPECT_EQ(std::find(rep.missing.begin(), rep.missing.end(), "data/test.png"),
              rep.missing.end());

    task_graph::OpenedProject opened;
    std::string err;
    ASSERT_TRUE(task_graph::open_project(tgp, ExtractDir("ex_m"), &opened, &err))
        << err;
    EXPECT_NE(std::find(opened.manifest.missing.begin(),
                        opened.manifest.missing.end(),
                        "/Users/whoever/tests/data/abs.png"),
              opened.manifest.missing.end());

    // writer 的出参（result.png）既不打包也不记 missing
    EXPECT_EQ(std::find(rep.missing.begin(), rep.missing.end(), "result.png"),
              rep.missing.end());
    for (const auto& a : rep.packed)
        EXPECT_NE(a.path, "result.png");
}

TEST_F(ProjectBundleTest, RemapsAbsoluteRefsIntoBundle)
{
    // 绝对路径引用指向真实文件（桌面经 Browse 选图的常见形态）：收进包内
    // assets/ 并把【包内图副本】的引用重写为包内相对路径——单文件通道
    // （wasm ?open/拖拽/分享）因此拿得到输入图片。writer 持同一路径的
    // file_path 不得重写（运行期输出会覆盖包内资产）；磁盘原 json 不动。
    const std::string abs = (root_ / "data/abs_input.png").string();
    WriteBytes(abs, asset_.data(), asset_.size());
    const std::string json = std::string(R"JSON({
  "version": "2.0",
  "tasks": [
    { "id": "src", "type": "opencv_image_read",
      "params": { "file_path": ")JSON") + abs + R"JSON(" } },
    { "id": "out", "type": "opencv_image_write",
      "params": { "file_path": ")JSON" + abs + R"JSON(" } }
  ],
  "edges": []
})JSON";
    const std::string graph = (root_ / "graphs/abs_test.json").string();
    WriteBytes(graph, json.data(), json.size());

    const std::string tgp = (root_ / "abs.tgp").string();
    const auto rep = task_graph::pack_project(graph, graph_dir_, tgp);
    ASSERT_TRUE(rep.ok) << rep.error;
    ASSERT_EQ(rep.remapped.size(), 1u);
    EXPECT_EQ(rep.remapped[0], abs);
    ASSERT_EQ(rep.packed.size(), 1u);
    EXPECT_EQ(rep.packed[0].path, "assets/abs_input.png");
    EXPECT_EQ(rep.packed[0].source, abs);
    EXPECT_TRUE(rep.missing.empty());

    // 磁盘上的原 graph.json 不动（只重写包内副本）
    const auto orig_back = ReadBytes(graph);
    EXPECT_TRUE(std::equal(orig_back.begin(), orig_back.end(), json.begin(),
                           json.end()));

    const std::string ex = ExtractDir("ex_abs");
    task_graph::OpenedProject opened;
    std::string err;
    ASSERT_TRUE(task_graph::open_project(tgp, ex, &opened, &err)) << err;
    ASSERT_EQ(opened.manifest.assets.size(), 1u);
    EXPECT_EQ(opened.manifest.assets[0].source, abs);

    // 解包布局：资产落在解包目录 assets/ 下，运行期首探即命中
    const std::string resolved =
        task_graph::resolve_asset_path(opened.extract_dir,
                                       "assets/abs_input.png");
    EXPECT_EQ(resolved,
              (fs::path(opened.extract_dir) / "assets/abs_input.png")
                  .lexically_normal().string());
    EXPECT_TRUE(fs::exists(resolved));
    const auto asset_back = ReadBytes(resolved);
    EXPECT_TRUE(asset_back == asset_);

    // 包内图副本：reader 引用已重写为包内相对路径（含 assets/ 字面量）；
    // 绝对路径原文只应剩 writer 那一处（原 json 中出现两次）
    const auto gbytes = ReadBytes(opened.graph_path);
    const std::string gtext(gbytes.begin(), gbytes.end());
    EXPECT_NE(gtext, json);  // 确为重写副本
    EXPECT_NE(gtext.find("\"assets/abs_input.png\""), std::string::npos);
    size_t pos = 0;
    int count = 0;
    while ((pos = gtext.find(abs, pos)) != std::string::npos) {
        ++count;
        pos += abs.size();
    }
    EXPECT_EQ(count, 1) << "writer 的 file_path 不应被重写";
}

TEST_F(ProjectBundleTest, RemapConflictDedupDotfiles)
{
    // 基名冲突去重的语义钉板（std::filesystem：最右点且非首字符才作扩展
    // 名分隔——前导点属文件名）：两个不同目录下的同名 ".cube"（LUT 点文
    // 件形态）收编为 assets/.cube 与 assets/.cube_2。pack_graph_project.py
    // 同构镜像的 parity 由 tests/test_pack_graph_project.py 的相同期望值
    // 锚定（此前 Python 侧 rpartition 会产出 "_2.cube"，无声分歧）。
    const std::string lut_a = (root_ / "lut_a/.cube").string();
    const std::string lut_b = (root_ / "lut_b/.cube").string();
    WriteBytes(lut_a, "LUT_A", 5);
    WriteBytes(lut_b, "LUT_B", 5);
    const std::string json = std::string(R"JSON({
  "version": "2.0",
  "tasks": [
    { "id": "lut1", "type": "render_lut_cube",
      "params": { "cube_path": ")JSON") + lut_a + R"JSON(" } },
    { "id": "lut2", "type": "render_lut_cube",
      "params": { "cube_path": ")JSON" + lut_b + R"JSON(" } }
  ],
  "edges": []
})JSON";
    const std::string graph = (root_ / "graphs/dot_test.json").string();
    WriteBytes(graph, json.data(), json.size());

    const std::string tgp = (root_ / "dot.tgp").string();
    const auto rep = task_graph::pack_project(graph, graph_dir_, tgp);
    ASSERT_TRUE(rep.ok) << rep.error;
    ASSERT_EQ(rep.remapped.size(), 2u);
    EXPECT_EQ(rep.remapped[0], lut_a);
    EXPECT_EQ(rep.remapped[1], lut_b);
    ASSERT_EQ(rep.packed.size(), 2u);
    EXPECT_EQ(rep.packed[0].path, "assets/.cube");
    EXPECT_EQ(rep.packed[1].path, "assets/.cube_2");
    EXPECT_EQ(rep.packed[0].source, lut_a);
    EXPECT_EQ(rep.packed[1].source, lut_b);
    EXPECT_TRUE(rep.missing.empty());

    const std::string ex = ExtractDir("ex_dot");
    task_graph::OpenedProject opened;
    std::string err;
    ASSERT_TRUE(task_graph::open_project(tgp, ex, &opened, &err)) << err;

    // 包内图副本：两个引用分别重写为各自的包内条目，绝对路径原文 0 次
    const auto gbytes = ReadBytes(opened.graph_path);
    const std::string gtext(gbytes.begin(), gbytes.end());
    EXPECT_NE(gtext.find("\"assets/.cube\""), std::string::npos);
    EXPECT_NE(gtext.find("\"assets/.cube_2\""), std::string::npos);
    EXPECT_EQ(gtext.find(lut_a), std::string::npos);
    EXPECT_EQ(gtext.find(lut_b), std::string::npos);

    // 资产内容按条目名各就各位（内容可区分，证明没有互覆）
    const auto back_a = ReadBytes((fs::path(ex) / "assets/.cube").string());
    const auto back_b = ReadBytes((fs::path(ex) / "assets/.cube_2").string());
    EXPECT_EQ(back_a, (std::vector<unsigned char>{'L', 'U', 'T', '_', 'A'}));
    EXPECT_EQ(back_b, (std::vector<unsigned char>{'L', 'U', 'T', '_', 'B'}));
}
