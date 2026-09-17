# copy_dll_if_exists.cmake — 存在才拷（cmake -E copy 对缺失源报错，会打断构建）。
# 用法：cmake -DSRC=<dll> -DDST=<dir> -P copy_dll_if_exists.cmake
if(EXISTS "${SRC}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SRC}" "${DST}")
    message(STATUS "Copied ${SRC} -> ${DST}")
else()
    message(STATUS "copy_dll_if_exists: ${SRC} 不存在，跳过（根构建树可能未产出）")
endif()
