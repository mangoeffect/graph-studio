#include <task_graph/data_types.hpp>
#include <gtest/gtest.h>
#include <string>

// 自定义类型注册
namespace myspace {
struct MyData {
    int x;
};
}

TG_REGISTER_TYPE(myspace::MyData, "myspace::MyData");

TEST(TypeRegistry, BuiltinTypesRegistered) {
    EXPECT_EQ(task_graph::type_name<task_graph::Image>(), "task_graph::Image");
    EXPECT_EQ(task_graph::type_name<task_graph::PointCloud>(), "task_graph::PointCloud");
    EXPECT_EQ(task_graph::type_name<int>(), "int");
    EXPECT_EQ(task_graph::type_name<std::string>(), "std::string");
}

TEST(TypeRegistry, UserTypeRegistered) {
    EXPECT_EQ(task_graph::type_name<myspace::MyData>(), "myspace::MyData");
}

TEST(TypeRegistry, UnregisteredTypeReturnsEmpty) {
    struct Unregistered {};
    EXPECT_EQ(task_graph::type_name<Unregistered>(), "");
}

TEST(TypeRegistry, MakePortCarriesTypeName) {
    auto spec = task_graph::make_port<task_graph::Image>("image");
    EXPECT_EQ(spec.name, "image");
    EXPECT_EQ(spec.type_name, "task_graph::Image");
    EXPECT_TRUE(spec.required);

    auto opt_spec = task_graph::make_port<int>("count", false);
    EXPECT_EQ(opt_spec.name, "count");
    EXPECT_EQ(opt_spec.type_name, "int");
    EXPECT_FALSE(opt_spec.required);
}
