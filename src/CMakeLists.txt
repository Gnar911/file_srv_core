cmake_minimum_required(VERSION 3.18)
project(file_service_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands so editors (clangd/VSCode) can evaluate headers
if(NOT DEFINED CMAKE_EXPORT_COMPILE_COMMANDS)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Generate compile_commands.json for editor tools" FORCE)
endif()

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
    asc_reader.cpp
    metadata_storage.cpp
    mmap/mmap_data.cpp
    can_decoder.cpp
    meta_data_tracker.cpp
    sqlite/can_log_decoder.cpp
    sqlite/sql_decode.cpp
    sqlite/sqlite_wrapper.cpp
    sqlite/log_index_db.cpp
    view_browser.cpp
    mmap/mmap_wrapper.cpp
    mmap/mmap_layout_view.cpp
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

    # Consolidate common test sources into a static library so tests don't
    # recompile the same production sources repeatedly.
    add_library(file_service_test_lib STATIC
        can_decoder.cpp
        sqlite/can_log_decoder.cpp
        sqlite/sql_decode.cpp
        sqlite/sqlite_wrapper.cpp
        sqlite/log_index_db.cpp
        metadata_storage.cpp
        meta_data_tracker.cpp
        view_browser.cpp
        mmap/mmap_data.cpp
        mmap/mmap_wrapper.cpp
        mmap/mmap_layout_view.cpp
    )

    target_include_directories(file_service_test_lib PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mmap
    )

    # Select sqlite link library (use absolute system path when available)
    if(UNIX AND EXISTS "/lib/x86_64-linux-gnu/libsqlite3.so.0")
        set(SQLITE_LIB "/lib/x86_64-linux-gnu/libsqlite3.so.0")
    else()
        set(SQLITE_LIB sqlite3)
    endif()

    target_link_libraries(file_service_test_lib PUBLIC ${SQLITE_LIB})

    # Helper to create test executables that link the common test library
    function(add_file_service_test targetName)
        add_executable(${targetName} ${ARGN})
        target_link_libraries(${targetName} PRIVATE file_service_test_lib GTest::gtest_main ${SQLITE_LIB})
        target_include_directories(${targetName} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/mmap
        )
        gtest_discover_tests(${targetName})
    endfunction()

    # Tests (only provide test-specific sources)
    #add_file_service_test(file_service_core_tests tests/out_segments_path_test.cpp)
    add_file_service_test(test_decode_function tests/test_decode_function.cpp)
    add_file_service_test(asc_reader_gtest tests/test_asc_reader.cpp asc_reader.cpp)
    add_file_service_test(run_worker_segmented_test tests/test_run_worker_segmented.cpp can_parser.cpp asc_reader.cpp)
    add_file_service_test(log_index_db_test tests/test_log_index_db.cpp)
    add_file_service_test(mmap_data_test tests/test_mmap_data.cpp)
    add_file_service_test(test_view_browser tests/test_view_browser.cpp)
endif()
