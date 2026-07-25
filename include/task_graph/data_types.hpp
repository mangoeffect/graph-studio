#pragma once

#include <vector>
#include <memory>
#include <string>
#include <any>
#include <optional>
#include <typeinfo>

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

struct Image {
    std::shared_ptr<std::vector<uint8_t>> data;
    uintptr_t gpu_handle{0};
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
    try {
        return std::any_cast<T>(value);
    } catch (const std::bad_any_cast&) {
        return std::nullopt;
    }
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

}
