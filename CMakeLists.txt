cmake_minimum_required(VERSION 2.8.12)

# Propagate arch to Rust build for CI Copied from
# rustyconover/duckdb-lindel-extension
set(Rust_CARGO_TARGET "")
if("${OS_NAME}" STREQUAL "linux")
    if ("${OS_ARCH}" STREQUAL "arm64")
        set(Rust_CARGO_TARGET "aarch64-unknown-linux-gnu")
    elseif("${CMAKE_CXX_COMPILER}" MATCHES "aarch64")
        set(Rust_CARGO_TARGET ${RUST_ENV_VARS} CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=aarch64-linux-gnu-gcc)
        set(Rust_CARGO_TARGET "aarch64-unknown-linux-gnu")
    else()
        string(FIND "${RUST_TARGETS}" "musl" MUSL_TARGET_FOUND)
        if(NOT MUSL_TARGET_FOUND EQUAL -1)
            set(Rust_CARGO_TARGET "x86_64-unknown-linux-musl")
        else()
            set(Rust_CARGO_TARGET "x86_64-unknown-linux-gnu")
        endif()
    endif()
elseif("${OS_NAME}" STREQUAL "osx")
    if ("${OSX_BUILD_ARCH}" STREQUAL "arm64")
        set(Rust_CARGO_TARGET "aarch64-apple-darwin")
    elseif ("${OSX_BUILD_ARCH}" STREQUAL "x86_64")
        set(Rust_CARGO_TARGET "x86_64-apple-darwin")
    elseif ("${OS_ARCH}" STREQUAL "arm64")
        set(Rust_CARGO_TARGET "aarch64-apple-darwin")
    endif()
elseif(WIN32)
   if (MINGW AND "${OS_ARCH}" STREQUAL "arm64")
       set(Rust_CARGO_TARGET "aarch64-pc-windows-gnu")
   elseif (MINGW AND "${OS_ARCH}" STREQUAL "amd64")
       set(Rust_CARGO_TARGET "x86_64-pc-windows-gnu")
   elseif (MSVC AND "${OS_ARCH}" STREQUAL "arm64")
       set(Rust_CARGO_TARGET "aarch64-pc-windows-msvc")
   elseif (MSVC AND "${OS_ARCH}" STREQUAL "amd64")
       set(Rust_CARGO_TARGET "x86_64-pc-windows-msvc")
   endif()
endif()

# We currently only support the predefined targets.
if("${Rust_CARGO_TARGET}" STREQUAL "")
  message(FATAL_ERROR "Failed to detect the correct platform")
endif()

set(TARGET_NAME prql)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})
include_directories(src/include)
include_directories(prql/prqlc/bindings/prqlc-c)

set(EXTENSION_SOURCES src/prql_extension.cpp)
add_library(${EXTENSION_NAME} STATIC ${EXTENSION_SOURCES})

if(EMSCRIPTEN)
  set(Rust_CARGO_TARGET "wasm32-unknown-emscripten")
endif()

add_subdirectory(corrosion)

corrosion_import_crate(
  MANIFEST_PATH
  prql/prqlc/bindings/prqlc-c/Cargo.toml
  CRATES
  prqlc-c
  CRATE_TYPES
  staticlib
  FLAGS
  --crate-type=staticlib)

set(PARAMETERS "-warnings")
build_loadable_extension(${TARGET_NAME} ${PARAMETERS} ${EXTENSION_SOURCES})

if(APPLE)
  target_link_libraries(${EXTENSION_NAME} prqlc_c-static
                        "-framework CoreFoundation")
else()
  target_link_libraries(${EXTENSION_NAME} prqlc_c-static)
endif()
target_link_libraries(${LOADABLE_EXTENSION_NAME} prqlc_c)

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
