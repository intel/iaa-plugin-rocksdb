// Copyright (C) 2022 Intel Corporation

// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <rocksdb/compressor.h>

namespace ROCKSDB_NAMESPACE {

std::unique_ptr<Compressor> NewIAACompressor();
}  // namespace ROCKSDB_NAMESPACE
