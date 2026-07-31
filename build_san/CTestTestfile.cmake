# CMake generated Testfile for 
# Source directory: /Users/wumango/Code/task_graph
# Build directory: /Users/wumango/Code/task_graph/build_san
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(test_dag "/Users/wumango/Code/task_graph/build_san/test_dag")
set_tests_properties(test_dag PROPERTIES  _BACKTRACE_TRIPLES "/Users/wumango/Code/task_graph/CMakeLists.txt;255;add_test;/Users/wumango/Code/task_graph/CMakeLists.txt;0;")
add_test(test_ports "/Users/wumango/Code/task_graph/build_san/test_ports")
set_tests_properties(test_ports PROPERTIES  _BACKTRACE_TRIPLES "/Users/wumango/Code/task_graph/CMakeLists.txt;256;add_test;/Users/wumango/Code/task_graph/CMakeLists.txt;0;")
add_test(test_params "/Users/wumango/Code/task_graph/build_san/test_params")
set_tests_properties(test_params PROPERTIES  _BACKTRACE_TRIPLES "/Users/wumango/Code/task_graph/CMakeLists.txt;257;add_test;/Users/wumango/Code/task_graph/CMakeLists.txt;0;")
add_test(test_serializer "/Users/wumango/Code/task_graph/build_san/test_serializer")
set_tests_properties(test_serializer PROPERTIES  _BACKTRACE_TRIPLES "/Users/wumango/Code/task_graph/CMakeLists.txt;258;add_test;/Users/wumango/Code/task_graph/CMakeLists.txt;0;")
add_test(test_data_types "/Users/wumango/Code/task_graph/build_san/test_data_types")
set_tests_properties(test_data_types PROPERTIES  _BACKTRACE_TRIPLES "/Users/wumango/Code/task_graph/CMakeLists.txt;259;add_test;/Users/wumango/Code/task_graph/CMakeLists.txt;0;")
add_test(test_gpu_image "/Users/wumango/Code/task_graph/build_san/test_gpu_image")
set_tests_properties(test_gpu_image PROPERTIES  _BACKTRACE_TRIPLES "/Users/wumango/Code/task_graph/CMakeLists.txt;260;add_test;/Users/wumango/Code/task_graph/CMakeLists.txt;0;")
add_test(test_task_params "/Users/wumango/Code/task_graph/build_san/test_task_params")
set_tests_properties(test_task_params PROPERTIES  _BACKTRACE_TRIPLES "/Users/wumango/Code/task_graph/CMakeLists.txt;261;add_test;/Users/wumango/Code/task_graph/CMakeLists.txt;0;")
add_test(test_profiler "/Users/wumango/Code/task_graph/build_san/test_profiler")
set_tests_properties(test_profiler PROPERTIES  _BACKTRACE_TRIPLES "/Users/wumango/Code/task_graph/CMakeLists.txt;262;add_test;/Users/wumango/Code/task_graph/CMakeLists.txt;0;")
add_test(test_type_registry "/Users/wumango/Code/task_graph/build_san/test_type_registry")
set_tests_properties(test_type_registry PROPERTIES  _BACKTRACE_TRIPLES "/Users/wumango/Code/task_graph/CMakeLists.txt;263;add_test;/Users/wumango/Code/task_graph/CMakeLists.txt;0;")
add_test(test_logger "/Users/wumango/Code/task_graph/build_san/test_logger")
set_tests_properties(test_logger PROPERTIES  _BACKTRACE_TRIPLES "/Users/wumango/Code/task_graph/CMakeLists.txt;264;add_test;/Users/wumango/Code/task_graph/CMakeLists.txt;0;")
add_test(test_plugin "/Users/wumango/Code/task_graph/build_san/test_plugin")
set_tests_properties(test_plugin PROPERTIES  _BACKTRACE_TRIPLES "/Users/wumango/Code/task_graph/CMakeLists.txt;265;add_test;/Users/wumango/Code/task_graph/CMakeLists.txt;0;")
subdirs("submodules/task1")
subdirs("submodules/task2")
subdirs("submodules/task_processor")
subdirs("submodules/image_filtering")
subdirs("submodules/image_reader")
subdirs("submodules/gpu_image_processing")
