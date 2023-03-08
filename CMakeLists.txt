# Copyright (C) 2022 Intel Corporation

# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.4)

set(iaa_compressor_SOURCES "iaa_compressor.cc" PARENT_SCOPE)
set(iaa_compressor_INCLUDE_PATHS "${QPL_PATH}/include" PARENT_SCOPE)
set(iaa_compressor_LINK_PATHS "${QPL_PATH}/lib" PARENT_SCOPE)
set(iaa_compressor_LIBS "qpl;accel-config;dl" PARENT_SCOPE)
set(iaa_compressor_CMAKE_EXE_LINKER_FLAGS "-u iaa_compressor_reg" PARENT_SCOPE)