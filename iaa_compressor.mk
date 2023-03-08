# Copyright (C) 2022 Intel Corporation

# SPDX-License-Identifier: Apache-2.0

iaa_compressor_SOURCES = iaa_compressor.cc
iaa_compressor_HEADERS = iaa_compressor.h
iaa_compressor_LDFLAGS = -lqpl -ldl -u iaa_compressor_reg