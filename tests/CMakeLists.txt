# Copyright (C) 2022 Intel Corporation

# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.15)

project(iaa_compressor_test VERSION 0.1.0)

option(COVERAGE "Enable test coverage report" OFF)
option(EXCLUDE_HW_TESTS "Exclude tests for hardware path, only runs tests on software path" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(iaa_compressor_test ../iaa_compressor.cc iaa_compressor_test.cc)

if(NOT DEFINED QPL_PATH)
  find_package(Qpl REQUIRED)
  if(Qpl_FOUND)
    message(STATUS "Found QPL: ${Qpl_DIR}")
    target_link_libraries(iaa_compressor_test Qpl::qpl)
  endif()
else()
  message(STATUS "Using QPL_PATH: ${QPL_PATH}")
  include_directories(${QPL_PATH}/include/qpl ${QPL_PATH}/include)
  target_link_directories(iaa_compressor_test PUBLIC ${QPL_PATH}/lib64 ${QPL_PATH}/lib)
  target_link_libraries(iaa_compressor_test qpl dl)
endif()

if(NOT DEFINED ROCKSDB_PATH)
  find_package(RocksDB REQUIRED)
  if(RocksDB_FOUND)
    message(STATUS "Found RocksDB: ${RocksDB_DIR}")
    target_link_libraries(iaa_compressor_test RocksDB)
  endif()
elseif(DEFINED ROCKSDB_PATH)
  message(STATUS "Using ROCKSDB_PATH: ${ROCKSDB_PATH}")
  include_directories(${ROCKSDB_PATH} ${ROCKSDB_PATH}/include)
  target_link_directories(iaa_compressor_test PUBLIC ${ROCKSDB_PATH})
  target_link_libraries(iaa_compressor_test rocksdb pthread)
endif()

find_package(GTest REQUIRED)
target_link_libraries(iaa_compressor_test gtest)

add_compile_definitions(ROCKSDB_PLATFORM_POSIX)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-rtti")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
if(COVERAGE)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage")
endif()
if(EXCLUDE_HW_TESTS)
  add_compile_definitions(EXCLUDE_HW_TESTS)
endif()

add_custom_target(run
    COMMAND LD_LIBRARY_PATH=${ROCKSDB_DIR}:${TBB_DIR}/lib/intel64/gcc4.8/ ./iaa_compressor_test
    DEPENDS iaa_compressor_test
)

add_custom_target(coverage
    COMMAND lcov --directory . --capture --output-file iaa_compressor.info && genhtml -o html iaa_compressor.info
)
