function(parse_subnode_json config_file)
    if(NOT EXISTS "${config_file}")
        message(WARNING "subnode.json not found: ${config_file}")
        return()
    endif()

    file(READ "${config_file}" SUBNODE_JSON)

    string(JSON SUBNODE_VERSION GET "${SUBNODE_JSON}" "version")
    message(STATUS "Subnode config version: ${SUBNODE_VERSION}")

    string(JSON SUBMODULES_COUNT LENGTH "${SUBNODE_JSON}" "submodules")
    
    if(SUBMODULES_COUNT EQUAL 0)
        message(STATUS "No submodules found")
        return()
    endif()

    message(STATUS "Found ${SUBMODULES_COUNT} submodules")

    set(SUBMODULES_LIST "")

    math(EXPR LAST_INDEX "${SUBMODULES_COUNT} - 1")
    
    foreach(index RANGE 0 ${LAST_INDEX})
        string(JSON SUBMODULE_NAME GET "${SUBNODE_JSON}" "submodules" ${index} "name")
        string(JSON SUBMODULE_URL GET "${SUBNODE_JSON}" "submodules" ${index} "url")
        string(JSON SUBMODULE_REF GET "${SUBNODE_JSON}" "submodules" ${index} "ref")
        string(JSON SUBMODULE_TYPE GET "${SUBNODE_JSON}" "submodules" ${index} "type")

        message(STATUS "Submodule: ${SUBMODULE_NAME} (${SUBMODULE_TYPE})")
        message(STATUS "  URL: ${SUBMODULE_URL}")
        message(STATUS "  Ref: ${SUBMODULE_REF}")

        string(JSON TASKS_COUNT LENGTH "${SUBNODE_JSON}" "submodules" ${index} "tasks")
        set(TASKS_LIST "")
        
        if(TASKS_COUNT GREATER 0)
            math(EXPR LAST_TASK_INDEX "${TASKS_COUNT} - 1")
            foreach(task_index RANGE 0 ${LAST_TASK_INDEX})
                string(JSON TASK_NAME GET "${SUBNODE_JSON}" "submodules" ${index} "tasks" ${task_index})
                list(APPEND TASKS_LIST "${TASK_NAME}")
            endforeach()
        endif()

        if(TASKS_LIST)
            message(STATUS "  Tasks: ${TASKS_LIST}")
        endif()

        set(SUBMODULE_${SUBMODULE_NAME}_NAME "${SUBMODULE_NAME}" PARENT_SCOPE)
        set(SUBMODULE_${SUBMODULE_NAME}_URL "${SUBMODULE_URL}" PARENT_SCOPE)
        set(SUBMODULE_${SUBMODULE_NAME}_REF "${SUBMODULE_REF}" PARENT_SCOPE)
        set(SUBMODULE_${SUBMODULE_NAME}_TYPE "${SUBMODULE_TYPE}" PARENT_SCOPE)
        set(SUBMODULE_${SUBMODULE_NAME}_TASKS "${TASKS_LIST}" PARENT_SCOPE)
        set(SUBMODULE_${SUBMODULE_NAME}_ENABLED ON PARENT_SCOPE)

        list(APPEND SUBMODULES_LIST "${SUBMODULE_NAME}")
    endforeach()

    set(SUBMODULES "${SUBMODULES_LIST}" PARENT_SCOPE)
endfunction()

function(add_subnode_module name)
    if(NOT SUBMODULE_${name}_ENABLED)
        return()
    endif()

    set(url "${SUBMODULE_${name}_URL}")
    set(ref "${SUBMODULE_${name}_REF}")
    set(type "${SUBMODULE_${name}_TYPE}")

    if("${type}" STREQUAL "local")
        get_filename_component(ABS_PATH "${url}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
        
        if(NOT EXISTS "${ABS_PATH}")
            message(WARNING "Local submodule path not found: ${ABS_PATH}")
            return()
        endif()

        message(STATUS "Adding local submodule: ${name} -> ${ABS_PATH}")
        
        if(EXISTS "${ABS_PATH}/CMakeLists.txt")
            add_subdirectory("${ABS_PATH}" "${CMAKE_BINARY_DIR}/submodules/${name}")
        else()
            message(WARNING "No CMakeLists.txt found in submodule: ${ABS_PATH}")
        endif()

    elseif("${type}" STREQUAL "git")
        message(STATUS "Git submodule not implemented: ${name}")
    else()
        message(WARNING "Unknown submodule type: ${type}")
    endif()
endfunction()
