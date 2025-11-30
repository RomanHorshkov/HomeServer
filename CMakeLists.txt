cmake_minimum_required(VERSION 3.20)

project(homeserver LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(default_build "Debug")
    set(CMAKE_BUILD_TYPE "${default_build}" CACHE STRING "Build type" FORCE)
    set_property(
        CACHE CMAKE_BUILD_TYPE
        PROPERTY STRINGS "Release" "RelWithDebInfo" "Debug" "MinSizeRel"
    )
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_BUILD_RPATH "${CMAKE_BINARY_DIR}/lib")

find_package(Threads REQUIRED)

# -----------------------------------------------------------------------------
# Logging dependency (EMlog)
# -----------------------------------------------------------------------------
set(EMLOG_BUILD_STATIC OFF CACHE BOOL "" FORCE)
set(EMLOG_BUILD_SHARED ON CACHE BOOL "" FORCE)
set(EMLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(app/external/EMlog)

# -----------------------------------------------------------------------------
# SPSC ring submodule
# -----------------------------------------------------------------------------
set(SPSCRING_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPSCRING_BUILD_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(app/external/SPSCring/app)

# -----------------------------------------------------------------------------
# llhttp (prebuilt)
# -----------------------------------------------------------------------------
function(add_prebuilt_static target include_dir lib_path)
    if(NOT EXISTS "${lib_path}")
        message(FATAL_ERROR "Missing prebuilt library: ${lib_path}")
    endif()
    add_library(${target} STATIC IMPORTED GLOBAL)
    set_target_properties(
        ${target}
        PROPERTIES
            IMPORTED_LOCATION "${lib_path}"
            INTERFACE_INCLUDE_DIRECTORIES "${include_dir}"
    )
endfunction()

add_prebuilt_static(
    llhttp
    ${CMAKE_SOURCE_DIR}/app/external/llhttp
    ${CMAKE_SOURCE_DIR}/app/external/llhttp/libllhttp.a
)

# -----------------------------------------------------------------------------
# Contract header (generated from contract/manifest.json, mirrors Makefile rule)
# -----------------------------------------------------------------------------
set(CONTRACT_MANIFEST ${CMAKE_SOURCE_DIR}/contract/manifest.json)
set(CONTRACT_HEADER ${CMAKE_SOURCE_DIR}/app/include/contract_version.h)
set(CONTRACT_GEN ${CMAKE_SOURCE_DIR}/utils/gen_contract_header.sh)
add_custom_command(
    OUTPUT ${CONTRACT_HEADER}
    COMMAND ${CMAKE_COMMAND} -E env bash ${CONTRACT_GEN}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS ${CONTRACT_MANIFEST} ${CONTRACT_GEN}
    COMMENT "Generating contract_version.h"
    VERBATIM
)
add_custom_target(contract_header DEPENDS ${CONTRACT_HEADER})

# -----------------------------------------------------------------------------
# Server sources / include paths
# -----------------------------------------------------------------------------
set(SERVER_SOURCES
    ${CMAKE_SOURCE_DIR}/app/src/main.c
    ${CMAKE_SOURCE_DIR}/app/src/core/core.c
    ${CMAKE_SOURCE_DIR}/app/src/core/reactor.c
    ${CMAKE_SOURCE_DIR}/app/src/core/pipeline.c
    ${CMAKE_SOURCE_DIR}/app/src/core/listener/listener.c
    ${CMAKE_SOURCE_DIR}/app/src/core/worker/worker.c
    ${CMAKE_SOURCE_DIR}/app/src/core/worker/dispatcher/dispatcher.c
    ${CMAKE_SOURCE_DIR}/app/src/core/worker/dispatcher/operator/operator.c
    ${CMAKE_SOURCE_DIR}/app/src/core/worker/dispatcher/operator/client/client.c
    ${CMAKE_SOURCE_DIR}/app/src/utils/socket_helper.c
    ${CMAKE_SOURCE_DIR}/app/src/utils/epoller.c
    ${CMAKE_SOURCE_DIR}/app/src/utils/time_helper.c
)

set(SERVER_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/app/include
    ${CMAKE_SOURCE_DIR}/app/include/core
    ${CMAKE_SOURCE_DIR}/app/include/core/listener
    ${CMAKE_SOURCE_DIR}/app/include/utils
    ${CMAKE_SOURCE_DIR}/app/include/core/worker
    ${CMAKE_SOURCE_DIR}/app/include/core/worker/dispatcher
    ${CMAKE_SOURCE_DIR}/app/include/core/worker/dispatcher/operator
    ${CMAKE_SOURCE_DIR}/app/include/core/worker/dispatcher/operator/client
    ${CMAKE_SOURCE_DIR}/app/include/core/worker/dispatcher/operator/client/browser
    ${CMAKE_SOURCE_DIR}/app/external/EMlog/app/include
    ${CMAKE_SOURCE_DIR}/app/external/SPSCring/app/include
    ${CMAKE_SOURCE_DIR}/app/external/llhttp
)

# -----------------------------------------------------------------------------
# Server target
# -----------------------------------------------------------------------------
add_executable(server ${SERVER_SOURCES})
add_dependencies(server contract_header)
target_include_directories(server PRIVATE ${SERVER_INCLUDE_DIRS})
set_target_properties(
    server
    PROPERTIES
        BUILD_RPATH "\$ORIGIN/../lib;${CMAKE_BINARY_DIR}/lib"
        INSTALL_RPATH "\$ORIGIN/../lib"
)
target_link_libraries(
    server
    PRIVATE
        emlog_shared
        spsc_ring_static
        llhttp
        Threads::Threads
)
target_compile_options(
    server
    PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:
            -Wall
            -Wextra
            -Wpedantic
            -Werror
        >
)
target_compile_definitions(server PRIVATE _POSIX_C_SOURCE=200809L)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(server PRIVATE DEBUG_MODE)
else()
    target_compile_definitions(server PRIVATE NDEBUG)
endif()
