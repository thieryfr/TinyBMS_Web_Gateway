cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(TinyBMS_WebGateway)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

idf_build_get_property(project_name PROJECT_NAME)
idf_build_get_property(project_ver PROJECT_VER)
idf_build_get_property(build_dir BUILD_DIR)

if(NOT project_ver)
    set(project_ver "0.0.0")
endif()

set(app_bin "${build_dir}/${project_name}.bin")

if(TARGET app)
    add_custom_command(
        TARGET app
        POST_BUILD
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/ota/package.py
                --manifest ${CMAKE_SOURCE_DIR}/ota/manifest.json
                --binary ${app_bin}
                --version ${project_ver}
                --project ${project_name}
                --update-manifest
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating OTA package metadata"
    )
else()
    message(WARNING "OTA packaging skipped: target 'app' not available")
endif()
