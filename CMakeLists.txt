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
# External database (build only the static lib, ensure vendored deps ready)
# -----------------------------------------------------------------------------
set(DB_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(DB_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(DB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(app/external/DataBase)

# -----------------------------------------------------------------------------
# Prebuilt helper libraries (llhttp, cjson, spsc_ring)
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
add_prebuilt_static(
    cjson
    ${CMAKE_SOURCE_DIR}/app/external/cjson
    ${CMAKE_SOURCE_DIR}/app/external/cjson/libcjson.a
)

# -----------------------------------------------------------------------------
# SPSC ring submodule
# -----------------------------------------------------------------------------
set(SPSCRING_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPSCRING_BUILD_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(app/external/SPSCring/app)

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
file(
    GLOB_RECURSE
    SERVER_SOURCES
    CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/app/src/*.c
)
if(SERVER_SOURCES STREQUAL "")
    message(FATAL_ERROR "No sources discovered under app/src")
endif()

set(APP_INCLUDE_ROOT ${CMAKE_SOURCE_DIR}/app/include)
file(GLOB_RECURSE SERVER_HEADER_PATHS RELATIVE ${APP_INCLUDE_ROOT} ${APP_INCLUDE_ROOT}/*.h)
set(SERVER_INCLUDE_DIRS ${APP_INCLUDE_ROOT})
foreach(header ${SERVER_HEADER_PATHS})
    get_filename_component(dir ${header} DIRECTORY)
    if(NOT dir OR dir STREQUAL ".")
        list(APPEND SERVER_INCLUDE_DIRS ${APP_INCLUDE_ROOT})
    else()
        list(APPEND SERVER_INCLUDE_DIRS ${APP_INCLUDE_ROOT}/${dir})
    endif()
endforeach()
list(REMOVE_DUPLICATES SERVER_INCLUDE_DIRS)

list(
    APPEND SERVER_INCLUDE_DIRS
        ${CMAKE_SOURCE_DIR}/app/external/llhttp
        ${CMAKE_SOURCE_DIR}/app/external/cjson
        ${CMAKE_SOURCE_DIR}/app/external/SPSCring/app/include
        ${CMAKE_SOURCE_DIR}/app/external/DataBase/app/include
        ${CMAKE_SOURCE_DIR}/app/external/DataBase/app/external/UUID7/include
)
if(EXISTS ${CMAKE_SOURCE_DIR}/app/external/DataBase/app/external/EMlog/app/include/emlog.h)
    list(APPEND SERVER_INCLUDE_DIRS
         ${CMAKE_SOURCE_DIR}/app/external/DataBase/app/external/EMlog/app/include)
elseif(EXISTS ${CMAKE_SOURCE_DIR}/app/external/DataBase/app/external/EMlog/include/emlog.h)
    list(APPEND SERVER_INCLUDE_DIRS
         ${CMAKE_SOURCE_DIR}/app/external/DataBase/app/external/EMlog/include)
endif()
list(REMOVE_DUPLICATES SERVER_INCLUDE_DIRS)

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
        db_app::static
        emlog_shared
        llhttp
        cjson
        spsc_ring_static
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
