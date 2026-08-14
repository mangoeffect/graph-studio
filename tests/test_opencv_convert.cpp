#ifdef TASK_GRAPH_ENABLE_OPENCV

#include <task_graph/data_types.hpp>
#include <opencv2/opencv.hpp>
#include <gtest/gtest.h>

TEST(OpenCvConvert, FromMat) {
    cv::Mat mat(100, 200, CV_8UC3, cv::Scalar(100, 150, 200));

    task_graph::Image img = task_graph::Image::from_mat(mat);

    ASSERT_TRUE(img.valid()) << "image invalid after from_mat";
    EXPECT_EQ(img.width, 200);
    EXPECT_EQ(img.height, 100);
    EXPECT_EQ(img.channels, 3);
    EXPECT_EQ(img.pixel_format, task_graph::PixelFormat::BGR);
}

TEST(OpenCvConvert, ToMat) {
    task_graph::Image img(320, 240, 3, task_graph::PixelFormat::BGR);

    cv::Mat mat = img.to_mat();

    ASSERT_FALSE(mat.empty()) << "mat is empty after to_mat";
    EXPECT_EQ(mat.cols, 320);
    EXPECT_EQ(mat.rows, 240);
    EXPECT_EQ(mat.channels(), 3);
}

TEST(OpenCvConvert, Roundtrip) {
    cv::Mat original(64, 64, CV_8UC3);
    cv::randu(original, 0, 255);

    task_graph::Image img = task_graph::Image::from_mat(original);
    cv::Mat restored = img.to_mat();

    // OpenCV 5.0 的 countNonZero 仅支持单通道；多通道差异用 norm 比较（0 表示完全一致）
    EXPECT_EQ(cv::norm(original, restored, cv::NORM_INF), 0.0) << "data mismatch after roundtrip";
}

TEST(OpenCvConvert, GrayImage) {
    cv::Mat mat(100, 100, CV_8UC1, cv::Scalar(128));

    task_graph::Image img = task_graph::Image::from_mat(mat);

    EXPECT_EQ(img.channels, 1);
    EXPECT_EQ(img.pixel_format, task_graph::PixelFormat::GRAY);

    cv::Mat restored = img.to_mat();
    EXPECT_EQ(restored.channels(), 1) << "restored should be single channel";
}

#endif  // TASK_GRAPH_ENABLE_OPENCV
