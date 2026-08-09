#pragma once

#include <vector>
#include <memory>
#include <string>
#include <any>
#include <variant>
#include <optional>
#include <typeinfo>
#include <typeindex>
#include <unordered_map>
#include <mutex>

#ifdef TASK_GRAPH_ENABLE_OPENCV
#include <opencv2/opencv.hpp>
#endif

namespace task_graph {

enum class PixelFormat {
    UNKNOWN = 0,
    GRAY = 1,
    RGB = 3,
    RGBA = 4,
    BGR = 3,
    BGRA = 4,
    YUV420 = 3,
    YUV422 = 4,
    YUV444 = 6
};

enum class DataType {
    UINT8 = 0,
    INT8 = 1,
    UINT16 = 2,
    INT16 = 3,
    UINT32 = 4,
    INT32 = 5,
    FLOAT32 = 6,
    FLOAT64 = 7
};

enum class MemoryLocation {
    CPU = 0,
    GPU = 1,
    BOTH = 2
};

class GpuBuffer;

struct Image {
    std::shared_ptr<std::vector<uint8_t>> data;
    uintptr_t gpu_handle{0};
    std::shared_ptr<GpuBuffer> gpu_buffer;
    int width{0};
    int height{0};
    int channels{0};
    PixelFormat pixel_format{PixelFormat::UNKNOWN};
    DataType data_type{DataType::UINT8};
    MemoryLocation location{MemoryLocation::CPU};
    std::string encoding;

    Image() = default;
    Image(int w, int h, int c, PixelFormat format = PixelFormat::RGB);
    Image(int w, int h, int c, const uint8_t* src_data, size_t size, 
          PixelFormat format = PixelFormat::RGB);

    size_t total_size() const;
    int bytes_per_channel() const;

    bool valid() const {
        if (location == MemoryLocation::CPU) {
            return data && width > 0 && height > 0 && channels > 0;
        }
        return gpu_handle != 0 && width > 0 && height > 0 && channels > 0;
    }

    bool is_on_gpu() const {
        return location == MemoryLocation::GPU || location == MemoryLocation::BOTH;
    }

    bool is_on_cpu() const {
        return location == MemoryLocation::CPU || location == MemoryLocation::BOTH;
    }

    uint8_t* ptr() {
        return data ? data->data() : nullptr;
    }

    const uint8_t* ptr() const {
        return data ? data->data() : nullptr;
    }

    bool to_gpu();
    bool to_cpu();
    bool ensure_cpu();
    bool ensure_gpu();

#ifdef TASK_GRAPH_ENABLE_OPENCV
    static Image from_mat(const cv::Mat& mat);
    cv::Mat to_mat() const;
#endif
};

struct Coordinate2D {
    double x{0.0};
    double y{0.0};

    Coordinate2D() = default;
    Coordinate2D(double x_, double y_) : x(x_), y(y_) {}

    bool valid() const {
        return x >= 0 && y >= 0;
    }
};

struct Coordinate3D {
    double x{0.0};
    double y{0.0};
    double z{0.0};

    Coordinate3D() = default;
    Coordinate3D(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    bool valid() const {
        return x >= 0 && y >= 0 && z >= 0;
    }
};

struct Point {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float intensity{0.0f};
    uint32_t timestamp{0};

    Point() = default;
    Point(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Point(float x_, float y_, float z_, float intensity_) 
        : x(x_), y(y_), z(z_), intensity(intensity_) {}
};

struct PointCloud {
    std::shared_ptr<std::vector<Point>> points;
    int width{0};
    int height{0};
    bool has_intensity{false};
    bool has_timestamp{false};
    std::string frame_id;
    uint64_t timestamp{0};

    PointCloud() = default;
    PointCloud(size_t num_points);
    PointCloud(int w, int h);

    size_t size() const {
        return points ? points->size() : 0;
    }

    bool valid() const {
        return points && !points->empty();
    }

    Point& operator[](size_t index) {
        return (*points)[index];
    }

    const Point& operator[](size_t index) const {
        return (*points)[index];
    }
};

template<typename T>
bool is_type(const std::any& value) {
    return value.type() == typeid(T);
}

inline bool is_image(const std::any& value) {
#ifdef TASK_GRAPH_ENABLE_OPENCV
    return is_type<cv::Mat>(value) || is_type<Image>(value);
#else
    return is_type<Image>(value);
#endif
}

inline bool is_coordinate2d(const std::any& value) {
    return is_type<Coordinate2D>(value);
}

inline bool is_coordinate3d(const std::any& value) {
    return is_type<Coordinate3D>(value);
}

inline bool is_pointcloud(const std::any& value) {
    return is_type<PointCloud>(value);
}

template<typename T>
std::optional<T> any_cast_safe(const std::any& value) {
    // type-check-first：WASM -fno-exceptions 下 any_cast 失败会 abort
    if (!value.has_value() || value.type() != typeid(T)) {
        return std::nullopt;
    }
    return std::any_cast<T>(value);
}

template<typename T>
std::optional<const T*> any_cast_ptr_safe(const std::any& value) {
    try {
        const T* ptr = std::any_cast<T>(&value);
        return ptr;
    } catch (const std::bad_any_cast&) {
        return std::nullopt;
    }
}

// ====================== Type Registry ======================
// 跨 so / 跨编译器稳定的类型名注册表。用 TG_REGISTER_TYPE 注册领域类型，
// 框架在构图期校验 PortSpec.type_name 时使用稳定名称，而非 typeid().name()。
namespace detail {

class TypeRegistry {
public:
    // 非 inline：定义在 data_types.cpp，强制链接器拉入该 TU（含 TG_REGISTER_TYPE 注册）
    static TypeRegistry& instance();

    void register_type(std::type_index idx, std::string name) {
        std::lock_guard<std::mutex> lock(mtx_);
        names_[idx] = std::move(name);
    }

    // 返回注册的稳定名称；未注册则返回空串（校验时按"未约束"处理）。
    std::string name_of(std::type_index idx) const {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = names_.find(idx);
        if (it != names_.end()) {
            return it->second;
        }
        return {};
    }

private:
    TypeRegistry() = default;
    mutable std::mutex mtx_;
    std::unordered_map<std::type_index, std::string> names_;
};

}  // namespace detail

// 在使用 T 的 cpp 文件作用域调用：TG_REGISTER_TYPE(MyType, "ns::MyType")
// 静态初始化期写入注册表，main() 执行前完成。
// 实现注意：T 可能含 "::"（如 std::string），不能直接 token-paste；
// 用 __LINE__ 保证同 TU 内多次调用产生的变量名唯一。
#define TG_TYPE_REG_PASTE2(a, b) a##b
#define TG_TYPE_REG_PASTE(a, b) TG_TYPE_REG_PASTE2(a, b)

// __attribute__((used)) 是 GCC/Clang 的强制发射（防链接器裁剪，尤其 WASM）；
// MSVC 无此语法（clang-cl 除外），且 /OPT:REF 不会裁剪带副作用初始化的
// 匿名 namespace 静态对象（它们由 CRT 初始化表锚定），去掉属性即可。
#if defined(_MSC_VER) && !defined(__clang__)
#define TG_REGISTER_TYPE(T, name)                                              \
    namespace {                                                                \
    inline const bool TG_TYPE_REG_PASTE(_tg_type_reg_, __LINE__) = [] {        \
        ::task_graph::detail::TypeRegistry::instance().register_type(          \
            std::type_index(typeid(T)), std::string(name));                    \
        return true;                                                           \
    }();                                                                       \
    }
#else
#define TG_REGISTER_TYPE(T, name)                                              \
    namespace {                                                                \
    __attribute__((used))                                                      \
    inline const bool TG_TYPE_REG_PASTE(_tg_type_reg_, __LINE__) = [] {        \
        ::task_graph::detail::TypeRegistry::instance().register_type(          \
            std::type_index(typeid(T)), std::string(name));                    \
        return true;                                                           \
    }();                                                                       \
    }
#endif

template <typename T>
inline std::string type_name() {
    return detail::TypeRegistry::instance().name_of(std::type_index(typeid(T)));
}

// ====================== PortSpec ======================
// 端口契约：task 显式声明的输入/输出端口规格。type_name 为 type_name<T>()
// 返回的稳定名；为空表示不校验类型，仅校验端口存在性。
struct PortSpec {
    std::string name;
    std::string type_name;
    bool required = true;
};

template <typename T>
inline PortSpec make_port(std::string name, bool required = true) {
    return PortSpec{std::move(name), type_name<T>(), required};
}

// ====================== ParamSpec ======================
// 参数契约：task 显式声明的可配置参数元信息，供 UI / 工具链自动发现
// 参数名、类型、默认值、取值范围与枚举可选值。与 PortSpec 对等。
enum class ParamType {
    Int,
    Float,
    String,
    Bool,
    Enum,   // 底层以 int 存储，enum_values 给出 label->value 映射
};

// 参数值类型（替代 std::any，消除跨 SO 的 RTTI/ABI 风险）
using ParamValue = std::variant<int, float, std::string, bool>;

struct ParamSpec {
    std::string name;                 // 参数键名，如 "kernel_size"
    ParamType   type{ParamType::Int};
    std::string description;          // 可选，UI tooltip
    ParamValue default_value;         // 按 type 取用 int/float/std::string/bool

    // 数值范围（仅 Int/Float 有意义；nullopt 表示无约束）
    std::optional<double> min_value;
    std::optional<double> max_value;
    std::optional<double> step;       // UI 步进，可选

    // 枚举可选值（type==Enum 时使用：label -> int 值）
    std::vector<std::pair<std::string, int>> enum_values;

    // UI 渲染提示（可选，不影响序列化/校验）。
    // widget_hint=="file" 时，UI 在 String 输入框旁渲染文件浏览按钮；
    // file_filter 为对话框过滤器，如 "Images (*.png *.jpg *.bmp)"。
    std::string widget_hint;
    std::string file_filter;

    bool required{false};

    // 类型安全取用 default_value（内部用 any_cast_safe，WASM 安全）
    std::optional<int> default_as_int() const;
    std::optional<float> default_as_float() const;
    std::optional<std::string> default_as_string() const;
    std::optional<bool> default_as_bool() const;
};

// ---- 工厂函数 ----
inline ParamSpec make_int_param(std::string name, int default_value,
                                std::optional<double> min_value = std::nullopt,
                                std::optional<double> max_value = std::nullopt,
                                std::optional<double> step = std::nullopt) {
    ParamSpec s;
    s.name = std::move(name);
    s.type = ParamType::Int;
    s.default_value = default_value;
    s.min_value = min_value;
    s.max_value = max_value;
    s.step = step;
    return s;
}

inline ParamSpec make_float_param(std::string name, float default_value,
                                  std::optional<double> min_value = std::nullopt,
                                  std::optional<double> max_value = std::nullopt,
                                  std::optional<double> step = std::nullopt) {
    ParamSpec s;
    s.name = std::move(name);
    s.type = ParamType::Float;
    s.default_value = default_value;
    s.min_value = min_value;
    s.max_value = max_value;
    s.step = step;
    return s;
}

inline ParamSpec make_string_param(std::string name, std::string default_value) {
    ParamSpec s;
    s.name = std::move(name);
    s.type = ParamType::String;
    s.default_value = std::move(default_value);
    return s;
}

// 文件路径参数：底层仍是 String，仅附加 UI hint 让面板渲染浏览按钮。
inline ParamSpec make_file_param(std::string name, std::string default_value = "",
                                 std::string file_filter = "All Files (*)") {
    ParamSpec s;
    s.name = std::move(name);
    s.type = ParamType::String;
    s.default_value = std::move(default_value);
    s.widget_hint = "file";
    s.file_filter = std::move(file_filter);
    return s;
}

inline ParamSpec make_bool_param(std::string name, bool default_value) {
    ParamSpec s;
    s.name = std::move(name);
    s.type = ParamType::Bool;
    s.default_value = default_value;
    return s;
}

inline ParamSpec make_enum_param(std::string name, int default_value,
                                 std::vector<std::pair<std::string, int>> enum_values) {
    ParamSpec s;
    s.name = std::move(name);
    s.type = ParamType::Enum;
    s.default_value = default_value;
    s.enum_values = std::move(enum_values);
    return s;
}

}
