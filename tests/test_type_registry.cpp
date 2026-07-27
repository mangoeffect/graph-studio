#include <task_graph/data_types.hpp>
#include <iostream>
#include <string>

// 自定义类型注册
namespace myspace {
struct MyData {
    int x;
};
}

TG_REGISTER_TYPE(myspace::MyData, "myspace::MyData");

namespace {

bool test_builtin_types_registered() {
    std::cout << "Test: builtin types registered... ";
    if (task_graph::type_name<task_graph::Image>() != "task_graph::Image") {
        std::cout << "FAIL (Image)\n";
        return false;
    }
    if (task_graph::type_name<task_graph::PointCloud>() != "task_graph::PointCloud") {
        std::cout << "FAIL (PointCloud)\n";
        return false;
    }
    if (task_graph::type_name<int>() != "int") {
        std::cout << "FAIL (int)\n";
        return false;
    }
    if (task_graph::type_name<std::string>() != "std::string") {
        std::cout << "FAIL (std::string)\n";
        return false;
    }
    std::cout << "OK\n";
    return true;
}

bool test_user_type_registered() {
    std::cout << "Test: user type registered... ";
    if (task_graph::type_name<myspace::MyData>() != "myspace::MyData") {
        std::cout << "FAIL\n";
        return false;
    }
    std::cout << "OK\n";
    return true;
}

bool test_unregistered_type_returns_empty() {
    std::cout << "Test: unregistered type returns empty string... ";
    struct Unregistered {};
    if (task_graph::type_name<Unregistered>() != "") {
        std::cout << "FAIL\n";
        return false;
    }
    std::cout << "OK\n";
    return true;
}

bool test_make_port_carries_type_name() {
    std::cout << "Test: make_port carries type_name... ";
    auto spec = task_graph::make_port<task_graph::Image>("image");
    if (spec.name != "image" || spec.type_name != "task_graph::Image" || !spec.required) {
        std::cout << "FAIL\n";
        return false;
    }
    auto opt_spec = task_graph::make_port<int>("count", false);
    if (opt_spec.name != "count" || opt_spec.type_name != "int" || opt_spec.required) {
        std::cout << "FAIL (optional)\n";
        return false;
    }
    std::cout << "OK\n";
    return true;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_builtin_types_registered();
    ok &= test_user_type_registered();
    ok &= test_unregistered_type_returns_empty();
    ok &= test_make_port_carries_type_name();
    std::cout << (ok ? "\nAll tests passed.\n" : "\nSome tests FAILED.\n");
    return ok ? 0 : 1;
}
