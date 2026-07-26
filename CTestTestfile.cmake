# CMake generated Testfile for 
# Source directory: /Users/wumango/Code/task_graph
# Build directory: /Users/wumango/Code/task_graph
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(test_dag "/Users/wumango/Code/task_graph/test_dag")
set_tests_properties(test_dag PROPERTIES  _BACKTRACE_TRIPLES "/Users/wumango/Code/task_graph/CMakeLists.txt;324;add_test;/Users/wumango/Code/task_graph/CMakeLists.txt;0;")
add_test(test_plugin "/Users/wumango/Code/task_graph/test_plugin")
set_tests_properties(test_plugin PROPERTIES  _BACKTRACE_TRIPLES "/Users/wumango/Code/task_graph/CMakeLists.txt;325;add_test;/Users/wumango/Code/task_graph/CMakeLists.txt;0;")
add_test(test_subnode "/Users/wumango/Code/task_graph/test_subnode")
set_tests_properties(test_subnode PROPERTIES  _BACKTRACE_TRIPLES "/Users/wumango/Code/task_graph/CMakeLists.txt;326;add_test;/Users/wumango/Code/task_graph/CMakeLists.txt;0;")
subdirs("submodules/task1")
subdirs("submodules/task2")
subdirs("submodules/task_processor")
subdirs("submodules/image_filtering")
