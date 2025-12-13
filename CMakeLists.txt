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
    ${CMAKE_SOURCE_DIR}/app/src/core/listener/listener.c
    ${CMAKE_SOURCE_DIR}/app/src/core/worker/worker.c
    ${CMAKE_SOURCE_DIR}/app/src/core/worker/operator/operator.c
    ${CMAKE_SOURCE_DIR}/app/src/core/worker/operator/client/client.c
    ${CMAKE_SOURCE_DIR}/app/src/core/worker/operator/client/http_man/http_manager.c
    ${CMAKE_SOURCE_DIR}/app/src/core/worker/operator/client/http_man/sanitizer.c
    ${CMAKE_SOURCE_DIR}/app/src/core/worker/operator/client/router/router.c
    ${CMAKE_SOURCE_DIR}/app/src/core/worker/operator/client/router/route_register.c
    ${CMAKE_SOURCE_DIR}/app/src/core/worker/operator/client/router/handlers/handler_db.c
    ${CMAKE_SOURCE_DIR}/app/src/core/worker/operator/client/router/handlers/handler_static.c
    ${CMAKE_SOURCE_DIR}/app/src/core/worker/operator/client/router/handlers/handler_utils.c
    ${CMAKE_SOURCE_DIR}/app/src/core/worker/operator/client/router/handlers/handler_whoami.c
    ${CMAKE_SOURCE_DIR}/app/src/utils/socket_helper.c
    ${CMAKE_SOURCE_DIR}/app/src/utils/epoller.c
    ${CMAKE_SOURCE_DIR}/app/src/utils/time_helper.c
)

set(SERVER_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/app/include
    ${CMAKE_SOURCE_DIR}/app/include/core
    ${CMAKE_SOURCE_DIR}/app/include/core/listener
    ${CMAKE_SOURCE_DIR}/app/include/core/worker
    ${CMAKE_SOURCE_DIR}/app/include/core/worker/operator
    ${CMAKE_SOURCE_DIR}/app/include/core/worker/operator/client
    ${CMAKE_SOURCE_DIR}/app/include/core/worker/operator/client
    ${CMAKE_SOURCE_DIR}/app/include/core/worker/operator/client/router
    ${CMAKE_SOURCE_DIR}/app/include/core/worker/operator/client/handlers
    ${CMAKE_SOURCE_DIR}/app/include/core/worker/operator/client/http_man
    ${CMAKE_SOURCE_DIR}/app/include/utils
    ${CMAKE_SOURCE_DIR}/app/external/DB_app/app/include
    ${CMAKE_SOURCE_DIR}/app/external/DB_app/app/external/yyjson
    ${CMAKE_SOURCE_DIR}/app/external/DB_app/app/external/UUID7/include
    ${CMAKE_SOURCE_DIR}/app/external/EMlog/app/include
    ${CMAKE_SOURCE_DIR}/app/external/SPSCring/app/include
    ${CMAKE_SOURCE_DIR}/app/external/llhttp
)

# Pull in all nested DB_app include folders so headers like db_config.h resolve.
file(GLOB_RECURSE DB_APP_INCLUDE_CANDIDATES LIST_DIRECTORIES true
     ${CMAKE_SOURCE_DIR}/app/external/DB_app/app/include/*)
set(DB_APP_INCLUDE_DIRS "")
foreach(path ${DB_APP_INCLUDE_CANDIDATES})
    if(IS_DIRECTORY ${path})
        list(APPEND DB_APP_INCLUDE_DIRS ${path})
    endif()
endforeach()
list(REMOVE_DUPLICATES DB_APP_INCLUDE_DIRS)
list(APPEND SERVER_INCLUDE_DIRS ${DB_APP_INCLUDE_DIRS})

# Prebuilt DB_app libraries (debug tree)
set(DB_PREBUILT_LIB_DIR $ENV{DB_APP_PREBUILT_DIR})
if(NOT DB_PREBUILT_LIB_DIR)
    set(DB_PREBUILT_LIB_DIR ${CMAKE_SOURCE_DIR}/app/external/DB_app/build_user_dbg/lib)
endif()
if(NOT EXISTS ${DB_PREBUILT_LIB_DIR}/libdb_app.a)
    # Fallback for legacy build path (may be read-only if produced by sudo; prefer build_user_dbg).
    set(DB_PREBUILT_LIB_DIR ${CMAKE_SOURCE_DIR}/app/external/DB_app/build_dbg/lib)
endif()
if(NOT EXISTS ${DB_PREBUILT_LIB_DIR}/libdb_app.a)
    message(FATAL_ERROR "Missing prebuilt db_app.a in ${DB_PREBUILT_LIB_DIR}; build the DB_app project (debug) first (export DB_APP_PREBUILT_DIR to override).")
endif()
add_library(db_app_prebuilt STATIC IMPORTED GLOBAL)
set_target_properties(db_app_prebuilt PROPERTIES
    IMPORTED_LOCATION ${DB_PREBUILT_LIB_DIR}/libdb_app.a
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/app/external/DB_app/app/include"
)
add_library(db_core_prebuilt STATIC IMPORTED GLOBAL)
set_target_properties(db_core_prebuilt PROPERTIES
    IMPORTED_LOCATION ${DB_PREBUILT_LIB_DIR}/libdb_core.a
)
add_library(uuid7_prebuilt STATIC IMPORTED GLOBAL)
set_target_properties(uuid7_prebuilt PROPERTIES
    IMPORTED_LOCATION ${DB_PREBUILT_LIB_DIR}/libuuid7.a
)
add_library(yyjson_prebuilt STATIC IMPORTED GLOBAL)
set_target_properties(yyjson_prebuilt PROPERTIES
    IMPORTED_LOCATION ${DB_PREBUILT_LIB_DIR}/libyyjson.a
)

find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_library(LMDB_LIBRARY NAMES lmdb)
if(NOT LMDB_LIBRARY)
    message(FATAL_ERROR "Unable to locate lmdb library")
endif()
find_library(SODIUM_LIBRARY NAMES sodium)
if(NOT SODIUM_LIBRARY)
    message(FATAL_ERROR "Unable to locate libsodium")
endif()

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
        "-Wl,--whole-archive"
        db_app_prebuilt
        db_core_prebuilt
        uuid7_prebuilt
        yyjson_prebuilt
        "-Wl,--no-whole-archive"
        OpenSSL::Crypto
        ${LMDB_LIBRARY}
        ${SODIUM_LIBRARY}
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
    target_compile_definitions(server PRIVATE MODE_DEBUG MODE_DEBUG)
else()
    target_compile_definitions(server PRIVATE NDEBUG)
endif()

option(HS_BUILD_HTTP_IT "Build the HTTP manager integration tests" OFF)
if(HS_BUILD_HTTP_IT)
    add_subdirectory(tests/IT)
endif()
