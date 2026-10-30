cmake_minimum_required(VERSION 3.10)

project(Memory-pool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING
        "Build type: Debug, Release, RelWithDebInfo or MinSizeRel" FORCE)
endif()

find_package(Threads REQUIRED)

add_library(memory_pool STATIC
    src/memory_pool.cc
    src/thread_cache.cc
)
target_include_directories(memory_pool PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(memory_pool PUBLIC Threads::Threads)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(memory_pool PRIVATE
        -Wall -Wextra -Wpedantic
    )
endif()

include(CTest)

if(BUILD_TESTING)
    add_executable(correctness_test
        test/correctness_test.cc
    )
    target_link_libraries(correctness_test PRIVATE memory_pool)

    add_executable(performance_test
        test/performance_test.cc
    )
    target_link_libraries(performance_test PRIVATE memory_pool)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(correctness_test PRIVATE
            -Wall -Wextra -Wpedantic
        )
        target_compile_options(performance_test PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()

    add_test(NAME correctness COMMAND correctness_test)
    set_tests_properties(correctness PROPERTIES TIMEOUT 120)
endif()
