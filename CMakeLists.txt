cmake_minimum_required(VERSION 2.8.12)

set(TARGET_NAME prql)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
project(${TARGET_NAME})
include_directories(src/include)
include_directories(prql/prql-lib)

set(EXTENSION_SOURCES src/prql_extension.cpp)
add_library(${EXTENSION_NAME} STATIC ${EXTENSION_SOURCES})

add_subdirectory(corrosion)

corrosion_import_crate(MANIFEST_PATH prql/Cargo.toml CRATES prql-lib CRATE_TYPES staticlib)

set(PARAMETERS "-warnings")
build_loadable_extension(${TARGET_NAME} ${PARAMETERS} ${EXTENSION_SOURCES})

target_link_libraries(${EXTENSION_NAME} prql-lib-static)
target_link_libraries(${TARGET_NAME}_loadable_extension prql-lib-static)

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
