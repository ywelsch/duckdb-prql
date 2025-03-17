# This file is included by DuckDB's build system. It specifies which extension to load

# Extension from this repo
duckdb_extension_load(prql
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    LINKED_LIBS "$(CMAKE_BINARY_DIR)/third_party/re2/libduckdb_re2.a" "$(CMAKE_BINARY_DIR)/extension/prql/libprqlc_c.a"
    LOAD_TESTS
)