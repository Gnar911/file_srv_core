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
    mmap/parsed_mmap.cpp
    mmap/mmap_data.cpp
    mmap/mmap_canid_idx.cpp
    mmap/mmap_chnl_idx.cpp
    mmap/mmap_dir_idx.cpp
    can_decoder.cpp
    sqlite/can_log_decoder.cpp
    sqlite/sql_decode.cpp
    mmap/mmap_wrapper.cpp
    mmap/mmap_header_constract.cpp
)

target_include_directories(file_service_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/mmap
)

if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(file_service_core PRIVATE Threads::Threads)
endif()

if(UNIX AND EXISTS "/lib/x86_64-linux-gnu/libsqlite3.so.0")
    target_link_libraries(file_service_core PRIVATE /lib/x86_64-linux-gnu/libsqlite3.so.0)
else()
    target_link_libraries(file_service_core PRIVATE sqlite3)
endif()

set_target_properties(file_service_core PROPERTIES
    OUTPUT_NAME file_service_core
)

find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(file_srv_core MODULE WITHOUT_SOABI
    pybind/parsed_mmap_pybind.cpp
    pybind/can_parser_pybind.cpp
)

target_include_directories(file_srv_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/mmap
    ${CMAKE_CURRENT_SOURCE_DIR}/pybind
)

if(NOT WIN32)
    target_link_libraries(file_srv_core PRIVATE file_service_core Threads::Threads)
else()
    target_link_libraries(file_srv_core PRIVATE file_service_core)
endif()

## Allow overriding the pybind output directory so the module can be built
## directly into the file_service package module directory.
set(PYBIND_OUTPUT_DIR "/home/gnar911/Desktop/20260516_JOBS_INSPECTOR/project/file_service/src/file_service/module" CACHE PATH "pybind output dir")

set_target_properties(file_srv_core PROPERTIES
    OUTPUT_NAME fs_core
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    BUILD_RPATH "$ORIGIN/.."
    LIBRARY_OUTPUT_DIRECTORY ${PYBIND_OUTPUT_DIR}
)

include(CTest)
if(BUILD_TESTING)
    find_package(GTest REQUIRED)
    include(GoogleTest)

    add_executable(file_service_core_tests
        tests/out_segments_path_test.cpp
        can_decoder.cpp
        sqlite/can_log_decoder.cpp
        sqlite/sql_decode.cpp
        mmap/parsed_mmap.cpp
        mmap/mmap_data.cpp
        mmap/mmap_canid_idx.cpp
        mmap/mmap_chnl_idx.cpp
        mmap/mmap_dir_idx.cpp
        mmap/mmap_wrapper.cpp
        mmap/mmap_header_constract.cpp
    )

    target_link_libraries(file_service_core_tests PRIVATE
        GTest::gtest
    )

    if(UNIX AND EXISTS "/lib/x86_64-linux-gnu/libsqlite3.so.0")
        target_link_libraries(file_service_core_tests PRIVATE /lib/x86_64-linux-gnu/libsqlite3.so.0)
    else()
        target_link_libraries(file_service_core_tests PRIVATE sqlite3)
    endif()

    target_include_directories(file_service_core_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mmap
    )

    gtest_discover_tests(file_service_core_tests)

    add_executable(test_decode_function
        tests/test_decode_function.cpp
        can_decoder.cpp
        sqlite/can_log_decoder.cpp
        sqlite/sql_decode.cpp
        mmap/parsed_mmap.cpp
        mmap/mmap_data.cpp
        mmap/mmap_canid_idx.cpp
        mmap/mmap_chnl_idx.cpp
        mmap/mmap_dir_idx.cpp
        #parsed_entry_layout.cpp
        mmap/mmap_wrapper.cpp
        mmap/mmap_header_constract.cpp
    )

    target_link_libraries(test_decode_function PRIVATE
        GTest::gtest
    )

    if(UNIX AND EXISTS "/lib/x86_64-linux-gnu/libsqlite3.so.0")
        target_link_libraries(test_decode_function PRIVATE /lib/x86_64-linux-gnu/libsqlite3.so.0)
    else()
        target_link_libraries(test_decode_function PRIVATE sqlite3)
    endif()

    target_include_directories(test_decode_function PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mmap
    )

    gtest_discover_tests(test_decode_function)
endif()
