#include <task_graph/data_types.hpp>
#include <task_graph/gpu_image_ops.hpp>

#ifdef TASK_GRAPH_ENABLE_OPENCV
#include <opencv2/opencv.hpp>
#endif

namespace task_graph {

// ====================== 内置类型注册 ======================
TG_REGISTER_TYPE(Image,        "task_graph::Image");
TG_REGISTER_TYPE(PointCloud,   "task_graph::PointCloud");
TG_REGISTER_TYPE(Coordinate2D, "task_graph::Coordinate2D");
TG_REGISTER_TYPE(Coordinate3D, "task_graph::Coordinate3D");
TG_REGISTER_TYPE(Point,        "task_graph::Point");
TG_REGISTER_TYPE(int,          "int");
TG_REGISTER_TYPE(float,        "float");
TG_REGISTER_TYPE(double,       "double");
TG_REGISTER_TYPE(std::string,  "std::string");
TG_REGISTER_TYPE(bool,         "bool");

#ifdef TASK_GRAPH_ENABLE_OPENCV
TG_REGISTER_TYPE(cv::Mat,      "cv::Mat");
#endif

// TypeRegistry::instance() 在此定义（非 inline），强制链接器拉入本 TU，
// 使上面的 TG_REGISTER_TYPE 静态初始化器在进程启动时执行（包括 WASM 单线程 build）。
}  // namespace task_graph
namespace task_graph::detail {
TypeRegistry& TypeRegistry::instance() {
    static TypeRegistry r;
    return r;
}
}  // namespace task_graph::detail
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

#ifdef TASK_GRAPH_ENABLE_OPENCV

static int cv_depth(DataType data_type);

static PixelFormat cv_type_to_pixel_format(int cv_type, int channels) {
    switch (channels) {
        case 1:
            return PixelFormat::GRAY;
        case 3: {
            if (cv_type == CV_8UC3 || cv_type == CV_16SC3 || cv_type == CV_16UC3 || 
                cv_type == CV_32SC3 || cv_type == CV_32FC3) {
                return PixelFormat::BGR;
            }
            return PixelFormat::RGB;
        }
        case 4: {
            if (cv_type == CV_8UC4 || cv_type == CV_16SC4 || cv_type == CV_16UC4 || 
                cv_type == CV_32SC4 || cv_type == CV_32FC4) {
                return PixelFormat::BGRA;
            }
            return PixelFormat::RGBA;
        }
        default:
            return PixelFormat::UNKNOWN;
    }
}

static DataType cv_type_to_data_type(int cv_type) {
    int depth = CV_MAT_DEPTH(cv_type);
    switch (depth) {
        case CV_8U:
            return DataType::UINT8;
        case CV_8S:
            return DataType::INT8;
        case CV_16U:
            return DataType::UINT16;
        case CV_16S:
            return DataType::INT16;
        case CV_32S:
            return DataType::INT32;
        case CV_32F:
            return DataType::FLOAT32;
        case CV_64F:
            return DataType::FLOAT64;
        default:
            return DataType::UINT8;
    }
}

Image Image::from_mat(const cv::Mat& mat) {
    if (mat.empty()) {
        return Image();
    }

    Image img;
    img.width = mat.cols;
    img.height = mat.rows;
    img.channels = mat.channels();
    img.pixel_format = cv_type_to_pixel_format(mat.type(), mat.channels());
    img.data_type = cv_type_to_data_type(mat.type());
    img.location = MemoryLocation::CPU;

    size_t size = static_cast<size_t>(mat.total()) * mat.elemSize();
    img.data = std::make_shared<std::vector<uint8_t>>(size);

    if (mat.isContinuous()) {
        std::memcpy(img.data->data(), mat.data, size);
    } else {
        size_t row_size = static_cast<size_t>(mat.cols) * mat.elemSize();
        for (int i = 0; i < mat.rows; ++i) {
            std::memcpy(img.data->data() + i * row_size, mat.ptr(i), row_size);
        }
    }

    return img;
}

cv::Mat Image::to_mat() const {
    if (!is_on_cpu() || !data || data->empty()) {
        return cv::Mat();
    }

    int cv_type = CV_MAKETYPE(cv_depth(data_type), channels);
    cv::Mat mat(height, width, cv_type, const_cast<uint8_t*>(data->data()));

    return mat.clone();
}

static int cv_depth(DataType data_type) {
    switch (data_type) {
        case DataType::UINT8:
            return CV_8U;
        case DataType::INT8:
            return CV_8S;
        case DataType::UINT16:
            return CV_16U;
        case DataType::INT16:
            return CV_16S;
        case DataType::INT32:
            return CV_32S;
        case DataType::FLOAT32:
            return CV_32F;
        case DataType::FLOAT64:
            return CV_64F;
        default:
            return CV_8U;
    }
}

#endif

}
