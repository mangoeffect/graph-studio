// Unit tests for resolve_path (see <task_graph/path_utils.hpp>).
// Covers: empty / absolute / relative paths, empty base_dir, dot-dot collapse.
//
// To avoid hardcoding path separators (Windows uses '\' from lexically_normal,
// POSIX uses '/'), expected values are constructed via std::filesystem::path
// so all assertions are cross-platform equivalent.
#include <task_graph/path_utils.hpp>
#include <filesystem>
#include <string>
#include "test_util.hpp"

using namespace task_graph;
namespace fs = std::filesystem;

// Build the expected "joined + normalized" string without hardcoding slashes.
static std::string joined(const std::string& base, const std::string& sub) {
    return (fs::path(base) / fs::path(sub)).lexically_normal().string();
}

TEST_CASE(resolve_empty_path_returns_empty) {
    EXPECT_EQ(resolve_path("/abs/dir", ""), std::string{});
    EXPECT_EQ(resolve_path("", ""), std::string{});
}

TEST_CASE(resolve_absolute_path_returned_as_is) {
    const std::string abs1 = "/usr/local/cfg";
    EXPECT_EQ(resolve_path("/var/data", abs1), fs::path(abs1).lexically_normal().string());

#ifdef _WIN32
    const std::string abs2 = "C:\\work\\cfg";
    EXPECT_EQ(resolve_path("C:\\other\\dir", abs2),
              fs::path(abs2).lexically_normal().string());
    const std::string abs3 = "C:/work/cfg";
    EXPECT_EQ(resolve_path("C:/other", abs3),
              fs::path(abs3).lexically_normal().string());
#endif
}

TEST_CASE(resolve_relative_path_prefixed_with_base_dir) {
    EXPECT_EQ(resolve_path("/home/u/graphs", "assets/test.png"),
              joined("/home/u/graphs", "assets/test.png"));
    EXPECT_EQ(resolve_path("/home/u/graphs/demo", "data/img.png"),
              joined("/home/u/graphs/demo", "data/img.png"));
#ifdef _WIN32
    EXPECT_EQ(resolve_path("C:\\graphs\\demo", "data\\img.png"),
              joined("C:\\graphs\\demo", "data\\img.png"));
    EXPECT_EQ(resolve_path("C:/graphs/demo", "data/img.png"),
              joined("C:/graphs/demo", "data/img.png"));
#endif
}

TEST_CASE(resolve_dotdot_collapsed) {
    EXPECT_EQ(resolve_path("/home/u/graphs", "./assets/../assets/x.png"),
              joined("/home/u/graphs", "assets/x.png"));
    EXPECT_EQ(resolve_path("/home/u/graphs", "sub/../assets/y.png"),
              joined("/home/u/graphs", "assets/y.png"));
}

TEST_CASE(resolve_empty_base_dir_keeps_original) {
    EXPECT_EQ(resolve_path("", "relative/path.png"),
              fs::path("relative/path.png").lexically_normal().string());
    EXPECT_EQ(resolve_path("", "/abs/path.png"),
              fs::path("/abs/path.png").lexically_normal().string());
    EXPECT_EQ(resolve_path("", ""), std::string{});
}

TEST_CASE(source_dir_param_constant) {
    EXPECT_TRUE(std::string(kSourceDirParam) == "_source_dir");
}

TEST_MAIN("Path Utils Tests")
