cmake_minimum_required(VERSION 3.20)

project(http_manager_integration_tests LANGUAGES C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CMOCKA QUIET cmocka)
if(NOT CMOCKA_FOUND)
    message(FATAL_ERROR "cmocka is required to build the HTTP manager integration tests. Install libcmocka-dev (or provide CMOCKA_* variables).")
endif()

add_library(cmocka::cmocka INTERFACE IMPORTED GLOBAL)
target_include_directories(cmocka::cmocka INTERFACE ${CMOCKA_INCLUDE_DIRS})
target_link_libraries(cmocka::cmocka INTERFACE ${CMOCKA_LIBRARIES})

set(HTTP_IT_SOURCES
    IT_http_man.c
    http_man_test_utils.c
    time/IT_http_man_time.c
)

add_executable(http_manager_integration_tests ${HTTP_IT_SOURCES})
target_include_directories(http_manager_integration_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${SERVER_INCLUDE_DIRS}
)
target_link_libraries(http_manager_integration_tests
    PRIVATE
        llhttp
        emlog_shared
        Threads::Threads
        cmocka::cmocka
)
add_test(NAME http_manager_integration_tests COMMAND http_manager_integration_tests)
