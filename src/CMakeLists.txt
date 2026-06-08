cmake_minimum_required(VERSION 3.18)
project(file_service_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PYBIND11_FINDPYTHON ON)
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

execute_process(
    COMMAND ${Python3_EXECUTABLE} -m pybind11 --cmakedir
    OUTPUT_VARIABLE pybind11_cmake_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE pybind11_cmake_dir_result
)

if(pybind11_cmake_dir_result EQUAL 0 AND EXISTS "${pybind11_cmake_dir}")
    list(PREPEND CMAKE_PREFIX_PATH "${pybind11_cmake_dir}")
endif()

add_library(file_service_core SHARED
    can_parser.cpp
    parsed_mmap.cpp
    mmap/mmap_data.cpp
    mmap/mmap_canid_idx.cpp
    mmap/mmap_chnl_idx.cpp
    mmap/mmap_dir_idx.cpp
    parsed_entry_layout.cpp
    can_decoder.cpp
    can_log_decoder.cpp
    mmap/mmap_wrapper.cpp
    mmap/mmap_header_constract.cpp
)

target_include_directories(file_service_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(file_service_core PRIVATE Threads::Threads)
endif()

set_target_properties(file_service_core PROPERTIES
    OUTPUT_NAME file_service_core
)

find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(parsed_mmap MODULE
    parsed_mmap_pybind.cpp
)

target_include_directories(parsed_mmap PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(NOT WIN32)
    target_link_libraries(parsed_mmap PRIVATE file_service_core Threads::Threads)
else()
    target_link_libraries(parsed_mmap PRIVATE file_service_core)
endif()

set_target_properties(parsed_mmap PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    BUILD_RPATH "$ORIGIN/.."
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/module
)

include(CTest)
if(BUILD_TESTING)
    find_package(GTest REQUIRED)
    include(GoogleTest)

    add_executable(file_service_core_tests
        tests/out_segments_path_test.cpp
        parsed_mmap.cpp
        mmap/mmap_data.cpp
        mmap/mmap_canid_idx.cpp
        mmap/mmap_chnl_idx.cpp
        mmap/mmap_dir_idx.cpp
        parsed_entry_layout.cpp
        mmap/mmap_wrapper.cpp
        mmap/mmap_header_constract.cpp
    )

    target_link_libraries(file_service_core_tests PRIVATE
        GTest::gtest
    )

    target_include_directories(file_service_core_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    gtest_discover_tests(file_service_core_tests)
endif()
