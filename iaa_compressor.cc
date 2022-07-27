// Copyright (C) 2022 Intel Corporation

// SPDX-License-Identifier: Apache-2.0

#include "iaa_compressor.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "logging/logging.h"
#include "qpl/qpl.h"
#include "rocksdb/compressor.h"
#include "rocksdb/configurable.h"
#include "rocksdb/env.h"
#include "rocksdb/utilities/options_type.h"
#include "util/coding.h"

namespace ROCKSDB_NAMESPACE {

// Error messages
#define MEMORY_ALLOCATION_ERROR "memory allocation error"
#define QPL_STATUS(status) "QPL status " + std::to_string(status)

extern "C" FactoryFunc<Compressor> iaa_compressor_reg;

FactoryFunc<Compressor> iaa_compressor_reg =
    ObjectLibrary::Default()->AddFactory<Compressor>(
        "com.intel.iaa_compressor_rocksdb",
        [](const std::string& /* uri */,
           std::unique_ptr<Compressor>* compressor, std::string* /* errmsg */) {
          *compressor = NewIAACompressor();
          return compressor->get();
        });

std::unordered_map<std::string, qpl_path_t> execution_paths{
    {"auto", qpl_path_auto},
    {"hw", qpl_path_hardware},
    {"sw", qpl_path_software}};

enum qpl_compression_mode { dynamic_mode, fixed_mode };

std::unordered_map<std::string, qpl_compression_mode> compression_modes{
    {"dynamic", dynamic_mode}, {"fixed", fixed_mode}};

struct IAACompressorOptions {
  static const char* kName() { return "IAACompressorOptions"; };
  qpl_path_t execution_path = qpl_path_auto;
  qpl_compression_mode compression_mode = dynamic_mode;
  bool verify = false;
  int level = 0;
  uint32_t parallel_threads = 1;
};

static std::unordered_map<std::string, OptionTypeInfo>
    iaa_compressor_type_info = {
        {"execution_path",
         OptionTypeInfo::Enum(
             offsetof(struct IAACompressorOptions, execution_path),
             &execution_paths)},
        {"compression_mode",
         OptionTypeInfo::Enum(
             offsetof(struct IAACompressorOptions, compression_mode),
             &compression_modes)},
        {"verify",
         {offsetof(struct IAACompressorOptions, verify), OptionType::kBoolean,
          OptionVerificationType::kNormal, OptionTypeFlags::kNone}},
        {"level",
         {offsetof(struct IAACompressorOptions, level), OptionType::kInt,
          OptionVerificationType::kNormal, OptionTypeFlags::kNone}},
        {"parallel_threads",
         {offsetof(struct IAACompressorOptions, parallel_threads),
          OptionType::kUInt32T, OptionVerificationType::kNormal,
          OptionTypeFlags::kNone}}};

class IAACompressor : public Compressor {
 public:
  IAACompressor() {
    RegisterOptions(&options_, &iaa_compressor_type_info);

#ifndef NDEBUG
    Status s =
        Env::Default()->NewLogger("/tmp/iaa_compressor_log.txt", &logger_);
    if (s.ok()) {
      logger_->SetInfoLogLevel(DEBUG_LEVEL);
    }
#endif
  };

  static const char* kClassName() { return "com.intel.iaa_compressor_rocksdb"; }

  const char* Name() const override { return kClassName(); }

  bool DictCompressionSupported() const override { return false; }

  uint32_t GetParallelThreads() const override {
    return options_.parallel_threads;
  };

  Status Compress(const CompressionInfo& /* info */, const Slice& input,
                  std::string* output) override {
    // Max size of a RocksDB block is 4GiB
    uint32_t output_header_length = EncodeSize(input.size(), output);

    // If data is incompressible, QPL returns stored blocks
    // A stored block is at most 2^16-1 bytes in size and it has a 5-byte header
    // So, in the worst case, data grows by 5*ceil(input.size()/65535)
    size_t input_length = input.size();
    size_t output_length =
        output_header_length + input_length +
        (input_length / 65535 + (input_length % 65535 != 0)) * 5;
    if (output_length > std::numeric_limits<uint32_t>::max()) {
      // Attempt compression with largest possible buffer. QPL will return an
      // error if not sufficient.
      output_length = std::numeric_limits<uint32_t>::max();
    }
    output->resize(output_length);

    qpl_status status;
    qpl_compression_levels level = GetQplLevel(options_.level);
    qpl_path_t execution_path = options_.execution_path;

    qpl_job* job;
    if (deflate_job_[execution_path] == nullptr) {
      uint32_t size;
      status = qpl_get_job_size(execution_path, &size);
      if (status != QPL_STS_OK) {
        return Status::Corruption(QPL_STATUS(status));
      }
      try {
        deflate_job_[execution_path] = std::make_unique<char[]>(size);
      } catch (std::bad_alloc& e) {
        return Status::Corruption(MEMORY_ALLOCATION_ERROR);
      }
      job = reinterpret_cast<qpl_job*>(deflate_job_[execution_path].get());
      status = qpl_init_job(execution_path, job);
      if (status != QPL_STS_OK) {
        return Status::Corruption(QPL_STATUS(status));
      }
    } else {
      job = reinterpret_cast<qpl_job*>(deflate_job_[execution_path].get());
    }

    uint8_t* source =
        const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(input.data()));
    uint8_t* destination =
        reinterpret_cast<uint8_t*>(&(*output)[0] + output_header_length);

    job->next_in_ptr = source;
    job->available_in = input.size();
    job->next_out_ptr = destination;
    job->available_out = output_length - output_header_length;
    job->level = level;
    job->op = qpl_op_compress;
    job->flags = QPL_FLAG_FIRST | QPL_FLAG_LAST | QPL_FLAG_OMIT_CHECKSUMS;
    if (!options_.verify) {
      job->flags |= QPL_FLAG_OMIT_VERIFY;
    }
    job->compression_huffman_table = nullptr;
    job->dictionary = nullptr;

    if (options_.compression_mode == dynamic_mode) {
      job->flags |= QPL_FLAG_DYNAMIC_HUFFMAN;
    }

    status = QPL_STS_QUEUES_ARE_BUSY_ERR;
    while (status == QPL_STS_QUEUES_ARE_BUSY_ERR) {
      status = qpl_execute_job(job);
    }

    if (status != QPL_STS_OK) {
      return Status::Corruption(QPL_STATUS(status));
    }
    output->resize(output_header_length + job->total_out);
    Debug(logger_, "Compress - input size: %lu - output size: %u\n",
          input.size(), job->total_out);

    return Status::OK();
  }

  Status Uncompress(const UncompressionInfo& info, const char* input,
                    size_t input_length, char** output,
                    size_t* output_length) override {
    // Extract uncompressed size
    uint32_t encoded_output_length = 0;
    if (!DecodeSize(&input, &input_length, &encoded_output_length)) {
      return Status::Corruption("size decoding error");
    }

    // Memory allocator may return null pointer or throw bad_alloc exception
    try {
      *output = Allocate(encoded_output_length, info.GetMemoryAllocator());
      if (*output == nullptr) {
        return Status::Corruption(MEMORY_ALLOCATION_ERROR);
      }
    } catch (std::bad_alloc& e) {
      return Status::Corruption(MEMORY_ALLOCATION_ERROR);
    }

    qpl_status status;
    qpl_job* job;
    if (inflate_job_[options_.execution_path] == nullptr) {
      uint32_t size;
      status = qpl_get_job_size(options_.execution_path, &size);
      if (status != QPL_STS_OK) {
        return Status::Corruption(QPL_STATUS(status));
      }
      try {
        inflate_job_[options_.execution_path] = std::make_unique<char[]>(size);
      } catch (std::bad_alloc& e) {
        return Status::Corruption(MEMORY_ALLOCATION_ERROR);
      }
      job = reinterpret_cast<qpl_job*>(
          inflate_job_[options_.execution_path].get());
      status = qpl_init_job(options_.execution_path, job);
      if (status != QPL_STS_OK) {
        return Status::Corruption(QPL_STATUS(status));
      }
    } else {
      job = reinterpret_cast<qpl_job*>(
          inflate_job_[options_.execution_path].get());
    }

    uint8_t* source =
        const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(input));
    uint8_t* destination = reinterpret_cast<uint8_t*>(*output);

    job->next_in_ptr = source;
    job->available_in = input_length;
    job->next_out_ptr = destination;
    job->available_out = encoded_output_length;
    job->op = qpl_op_decompress;
    job->decompression_huffman_table = nullptr;
    job->flags = QPL_FLAG_FIRST | QPL_FLAG_LAST;
    job->flags |= QPL_FLAG_OMIT_CHECKSUMS;

    status = QPL_STS_QUEUES_ARE_BUSY_ERR;
    while (status == QPL_STS_QUEUES_ARE_BUSY_ERR) {
      status = qpl_execute_job(job);
    }

    if (status != QPL_STS_OK) {
      return Status::Corruption(QPL_STATUS(status));
    } else if (job->total_out != encoded_output_length) {
      return Status::Corruption("size mismatch");
    }
    *output_length = job->total_out;
    Debug(logger_, "Uncompress - input size: %lu - output size: %u\n",
          input_length, job->total_out);

    return Status::OK();
  }

  bool IsDictEnabled() const override { return false; }

 private:
  IAACompressorOptions options_;
  static thread_local std::vector<std::unique_ptr<char[]>> deflate_job_;
  static thread_local std::vector<std::unique_ptr<char[]>> inflate_job_;
  std::shared_ptr<Logger> logger_;

  uint32_t EncodeSize(size_t length, std::string* output) {
    PutVarint32(output, length);
    return output->size();
  }

  bool DecodeSize(const char** input, size_t* input_length,
                  uint32_t* output_length) {
    auto new_input =
        GetVarint32Ptr(*input, *input + *input_length, output_length);
    if (new_input == nullptr) {
      return false;
    }
    *input_length -= (new_input - *input);
    *input = new_input;
    return true;
  }

  int GetLevel() const override { return options_.level; }

  qpl_compression_levels GetQplLevel(int level) {
    if (level == 0 || level == CompressionOptions::kDefaultCompressionLevel) {
      return qpl_default_level;
    } else {
      return qpl_high_level;
    }
  }
};

// Reuse job structs across calls. Have one struct per thread and execution path
// (hw, sw, auto).
thread_local std::vector<std::unique_ptr<char[]>> IAACompressor::deflate_job_(
    3);
thread_local std::vector<std::unique_ptr<char[]>> IAACompressor::inflate_job_(
    3);

std::unique_ptr<Compressor> NewIAACompressor() {
  return std::unique_ptr<Compressor>(new IAACompressor());
}

}  // namespace ROCKSDB_NAMESPACE
