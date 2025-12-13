cmake_minimum_required(VERSION 3.20)

project(http_manager_integration_tests LANGUAGES C)

enable_testing()

find_package(PkgConfig REQUIRED)
pkg_check_modules(CMOCKA QUIET cmocka)
if(NOT CMOCKA_FOUND)
    message(FATAL_ERROR "cmocka is required to build the HTTP manager integration tests. Install libcmocka-dev (or provide CMOCKA_* variables).")
endif()

add_library(cmocka::cmocka INTERFACE IMPORTED GLOBAL)
target_include_directories(cmocka::cmocka INTERFACE ${CMOCKA_INCLUDE_DIRS})
target_link_libraries(cmocka::cmocka INTERFACE ${CMOCKA_LIBRARIES})

set(HTTP_MANAGER_SOURCES
    ${CMAKE_SOURCE_DIR}/app/src/core/worker/operator/client/http_man/http_manager.c
    ${CMAKE_SOURCE_DIR}/app/src/core/worker/operator/client/http_man/sanitizer.c
)

add_library(http_manager_impl STATIC ${HTTP_MANAGER_SOURCES})
target_include_directories(http_manager_impl PUBLIC ${SERVER_INCLUDE_DIRS})

add_library(http_man_test_utils STATIC http_man_test_utils.c)
target_include_directories(http_man_test_utils PUBLIC ${SERVER_INCLUDE_DIRS})

add_executable(http_manager_integration_tests IT_http_man.c)
target_include_directories(http_manager_integration_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(http_manager_integration_tests
    PRIVATE
        http_manager_impl
        http_man_test_utils
        llhttp_shared
        emlog_shared
        Threads::Threads
        cmocka::cmocka
)
add_test(NAME http_manager_integration_tests COMMAND http_manager_integration_tests)

add_executable(http_manager_timing_tests time/IT_http_man_time.c)
target_include_directories(http_manager_timing_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(http_manager_timing_tests
    PRIVATE
        http_manager_impl
        http_man_test_utils
        llhttp_shared
        emlog_shared
        Threads::Threads
        cmocka::cmocka
)
add_test(NAME http_manager_timing_tests COMMAND http_manager_timing_tests)
