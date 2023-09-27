.PHONY: all clean format debug release duckdb_debug duckdb_release pull update

all: release

MKFILE_PATH := $(abspath $(lastword $(MAKEFILE_LIST)))
PROJ_DIR := $(dir $(MKFILE_PATH))

ifeq (${STATIC_LIBCPP}, 1)
	STATIC_LIBCPP=-DSTATIC_LIBCPP=TRUE
endif

CMAKE_OSX_ARCHITECTURES_FLAG=
ifneq (${CMAKE_OSX_ARCHITECTURES},)
	CMAKE_OSX_ARCHITECTURES_FLAG=-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}
endif

Rust_CARGO_TARGET_FLAG=
ifneq ($(Rust_CARGO_TARGET),)
	Rust_CARGO_TARGET_FLAG=-DRust_CARGO_TARGET=${Rust_CARGO_TARGET}
endif

ifeq ($(GEN),ninja)
	GENERATOR=-G "Ninja"
	FORCE_COLOR=-DFORCE_COLORED_OUTPUT=1
endif

#### Configuration for this extension
EXTENSION_NAME=PRQL
EXTENSION_FLAGS=\
-DDUCKDB_EXTENSION_NAMES="prql" \
-DDUCKDB_EXTENSION_${EXTENSION_NAME}_PATH="$(PROJ_DIR)" \
-DDUCKDB_EXTENSION_${EXTENSION_NAME}_LOAD_TESTS=1 \
-DDUCKDB_EXTENSION_${EXTENSION_NAME}_INCLUDE_PATH="$(PROJ_DIR)src/include" \
-DDUCKDB_EXTENSION_${EXTENSION_NAME}_TEST_PATH="$(PROJ_DIR)test/sql"

BUILD_FLAGS=${STATIC_LIBCPP} $(EXTENSION_FLAGS) ${CMAKE_OSX_ARCHITECTURES_FLAG} ${Rust_CARGO_TARGET_FLAG}

ifeq (${EXTENSION_STATIC_BUILD}, 1)
	BUILD_FLAGS:=${BUILD_FLAGS} -DEXTENSION_STATIC_BUILD=1
endif

CLIENT_FLAGS=-DDUCKDB_EXTENSION_${EXTENSION_NAME}_SHOULD_LINK=1

# These flags will make DuckDB build the extension
EXTENSION_FLAGS=-DDUCKDB_OOT_EXTENSION_NAMES="prql" -DDUCKDB_OOT_EXTENSION_PRQL_PATH="$(PROJ_DIR)" -DDUCKDB_OOT_EXTENSION_PRQL_SHOULD_LINK="TRUE" -DDUCKDB_OOT_EXTENSION_PRQL_INCLUDE_PATH="$(PROJ_DIR)src/include"

pull:
	git submodule init
	git submodule update --recursive

clean:
	rm -rf build
	rm -rf testext
	cd duckdb && make clean

# Main build
debug:
	mkdir -p  build/debug && \
	cmake $(GENERATOR) $(FORCE_COLOR) $(EXTENSION_FLAGS) ${CLIENT_FLAGS} -DCMAKE_BUILD_TYPE=Debug ${BUILD_FLAGS} -S ./duckdb/ -B build/debug && \
	cmake --build build/debug --config Debug

release:
	mkdir -p build/release && \
	cmake $(GENERATOR) $(FORCE_COLOR) $(EXTENSION_FLAGS) ${CLIENT_FLAGS} -DCMAKE_BUILD_TYPE=Release ${BUILD_FLAGS} -S ./duckdb/ -B build/release && \
	cmake --build build/release --config Release

# Main tests
test: test_release

test_release: release
	./build/release/test/unittest --test-dir . "[sql]"

test_debug: debug
	./build/debug/test/unittest --test-dir . "[sql]"

format:
	find src/ -iname *.hpp -o -iname *.cpp | xargs clang-format --sort-includes=0 -style=file -i
	cmake-format -i CMakeLists.txt

update:
	git submodule update --remote --merge
