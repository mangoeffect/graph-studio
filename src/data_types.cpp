#include <task_graph/data_types.hpp>
#include <task_graph/gpu_image_ops.hpp>

namespace task_graph {

Image::Image(int w, int h, int c, PixelFormat format)
    : width(w), height(h), channels(c), pixel_format(format) {
    size_t size = static_cast<size_t>(w) * h * c;
    data = std::make_shared<std::vector<uint8_t>>(size);
}

Image::Image(int w, int h, int c, const uint8_t* src_data, size_t size, 
             PixelFormat format)
    : width(w), height(h), channels(c), pixel_format(format) {
    data = std::make_shared<std::vector<uint8_t>>(src_data, src_data + size);
}

size_t Image::total_size() const {
    return static_cast<size_t>(width) * height * channels * bytes_per_channel();
}

int Image::bytes_per_channel() const {
    switch (data_type) {
        case DataType::UINT8:
        case DataType::INT8:
            return 1;
        case DataType::UINT16:
        case DataType::INT16:
            return 2;
        case DataType::UINT32:
        case DataType::INT32:
            return 4;
        case DataType::FLOAT32:
            return 4;
        case DataType::FLOAT64:
            return 8;
        default:
            return 1;
    }
}

PointCloud::PointCloud(size_t num_points) {
    points = std::make_shared<std::vector<Point>>(num_points);
    width = static_cast<int>(num_points);
    height = 1;
}

PointCloud::PointCloud(int w, int h) : width(w), height(h) {
    points = std::make_shared<std::vector<Point>>(static_cast<size_t>(w) * h);
}

bool Image::to_gpu() {
    return task_graph::to_gpu(*this);
}

bool Image::to_cpu() {
    return task_graph::to_cpu(*this);
}

bool Image::ensure_cpu() {
    return task_graph::ensure_cpu(*this);
}

bool Image::ensure_gpu() {
    return task_graph::ensure_gpu(*this);
}

}
