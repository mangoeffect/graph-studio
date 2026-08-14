// 领域数据类型测试：Image / Coordinate2D / Coordinate3D / PointCloud / any_cast。
// 纯结构与类型判定，不含执行流（Image 的 check_input 执行测试已移至 test_ports.cpp）。
#include <task_graph/data_types.hpp>
#include <any>
#include <gtest/gtest.h>

using namespace task_graph;

TEST(DataTypes, image_creation_and_any_cast) {
    Image img(640, 480, 3);
    EXPECT_TRUE(img.valid());
    EXPECT_EQ(img.total_size(), size_t(640 * 480 * 3));

    std::any any_img = img;
    auto casted = any_cast_safe<Image>(any_img);
    EXPECT_TRUE(casted.has_value());
    EXPECT_TRUE(is_image(any_img));
}

TEST(DataTypes, coordinate_types) {
    Coordinate2D c2(10.5, 20.3);
    EXPECT_TRUE(c2.valid());
    std::any a2 = c2;
    EXPECT_TRUE(is_coordinate2d(a2));

    Coordinate3D c3(1.0, 2.0, 3.0);
    EXPECT_TRUE(c3.valid());
    std::any a3 = c3;
    EXPECT_TRUE(is_coordinate3d(a3));
}

TEST(DataTypes, pointcloud_type) {
    PointCloud cloud(1000);
    EXPECT_TRUE(cloud.valid());
    EXPECT_EQ(cloud.size(), size_t(1000));

    cloud[0] = Point(1.0f, 2.0f, 3.0f, 0.8f);
    EXPECT_EQ(cloud[0].x, 1.0f);
    EXPECT_EQ(cloud[0].intensity, 0.8f);

    std::any any_cloud = cloud;
    EXPECT_TRUE(is_pointcloud(any_cloud));
    auto casted = any_cast_safe<PointCloud>(any_cloud);
    EXPECT_TRUE(casted.has_value());
    EXPECT_EQ(casted->size(), size_t(1000));
}

