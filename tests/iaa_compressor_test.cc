// Copyright (C) 2022 Intel Corporation

// SPDX-License-Identifier: Apache-2.0

#include "../iaa_compressor.h"

#include <gtest/gtest.h>

#include <cmath>
#include <iostream>
#include <tuple>

#include "rocksdb/convenience.h"
#include "util/coding.h"

namespace ROCKSDB_NAMESPACE {

TEST(Options, DefaultOptions) {
  std::shared_ptr<Compressor> compressor;
  ConfigOptions config_options;
  Status s = Compressor::CreateFromString(
      config_options, "id=com.intel.iaa_compressor_rocksdb", &compressor);
  ASSERT_TRUE(s.ok());

  std::string value;
  s = compressor->GetOption(config_options, "execution_path", &value);
  ASSERT_TRUE(s.ok());
  ASSERT_EQ(value, "auto");
  s = compressor->GetOption(config_options, "compression_mode", &value);
  ASSERT_TRUE(s.ok());
  ASSERT_EQ(value, "dynamic");
  s = compressor->GetOption(config_options, "verify", &value);
  ASSERT_TRUE(s.ok());
  ASSERT_EQ(value, "false");
  s = compressor->GetOption(config_options, "level", &value);
  ASSERT_TRUE(s.ok());
  ASSERT_EQ(value, "0");
  s = compressor->GetOption(config_options, "parallel_threads", &value);
  ASSERT_TRUE(s.ok());
  ASSERT_EQ(value, "1");
}

TEST(Options, NonDefaultOptions) {
  std::shared_ptr<Compressor> compressor;
  ConfigOptions config_options;
  Status s =
      Compressor::CreateFromString(config_options,
                                   "id=com.intel.iaa_compressor_rocksdb;"
                                   "execution_path=hw;compression_mode=fixed;"
                                   "verify=true;level=1;parallel_threads=2",
                                   &compressor);
  ASSERT_TRUE(s.ok());

  std::string value;
  s = compressor->GetOption(config_options, "execution_path", &value);
  ASSERT_TRUE(s.ok());
  ASSERT_EQ(value, "hw");
  s = compressor->GetOption(config_options, "compression_mode", &value);
  ASSERT_TRUE(s.ok());
  ASSERT_EQ(value, "fixed");
  s = compressor->GetOption(config_options, "verify", &value);
  ASSERT_TRUE(s.ok());
  ASSERT_EQ(value, "true");
  s = compressor->GetOption(config_options, "level", &value);
  ASSERT_TRUE(s.ok());
  ASSERT_EQ(value, "1");
  s = compressor->GetOption(config_options, "parallel_threads", &value);
  ASSERT_TRUE(s.ok());
  ASSERT_EQ(value, "2");
}

TEST(Options, InvalidOptions) {
  std::string invalid_options =
      "id=com.intel.iaa_compressor_rocksdb;"
      "execution_path=aaa;compression_mode=aaa;"
      "verify=aaa;level=aaa;parallel_threads=aaa";

  // If not ignoring unknown options, an error will be reported
  std::shared_ptr<Compressor> compressor;
  ConfigOptions config_options;
  Status s = Compressor::CreateFromString(config_options, invalid_options,
                                          &compressor);
  ASSERT_TRUE(s.IsInvalidArgument());

  // If ignoring unknown options, options will be default
  config_options.ignore_unknown_options = true;
  s = Compressor::CreateFromString(config_options, invalid_options,
                                   &compressor);
  ASSERT_TRUE(s.ok());

  std::string value;
  s = compressor->GetOption(config_options, "execution_path", &value);
  ASSERT_TRUE(s.ok());
  ASSERT_EQ(value, "auto");
  s = compressor->GetOption(config_options, "compression_mode", &value);
  ASSERT_TRUE(s.ok());
  ASSERT_EQ(value, "dynamic");
  s = compressor->GetOption(config_options, "verify", &value);
  ASSERT_TRUE(s.ok());
  ASSERT_EQ(value, "false");
  s = compressor->GetOption(config_options, "level", &value);
  ASSERT_TRUE(s.ok());
  ASSERT_EQ(value, "0");
  s = compressor->GetOption(config_options, "parallel_threads", &value);
  ASSERT_TRUE(s.ok());
  ASSERT_EQ(value, "1");
}

char* GenerateBlock(size_t length, int seed = 0) {
  char* buf = (char*)malloc(length);
  if (!buf) {
    return nullptr;
  }
  for (unsigned int i = 0; i < length; i++) {
    buf[i] = 'a' + ((i + seed) % 26);
  }
  return buf;
}

void DestroyBlock(char* buf) { free(buf); }

class NullMemoryAllocator : public MemoryAllocator {
 public:
  static const char* kClassName() { return "NullMemoryAllocator"; }
  const char* Name() const override { return kClassName(); }

  void* Allocate(size_t /*size*/) override { return nullptr; }

  void Deallocate(void* /*p*/) override {}
};

class ExceptionMemoryAllocator : public MemoryAllocator {
 public:
  static const char* kClassName() { return "ExceptionMemoryAllocator"; }
  const char* Name() const override { return kClassName(); }

  void* Allocate(size_t /*size*/) override { throw std::bad_alloc(); }

  void Deallocate(void* /*p*/) override {}
};

TEST(ErrorConditions, AllocationError) {
  size_t input_length = 1024;
  char* input = GenerateBlock(input_length);
  ASSERT_NE(input, nullptr);

  std::shared_ptr<Compressor> compressor;
  ConfigOptions config_options;
  Status s = Compressor::CreateFromString(
      config_options, "id=com.intel.iaa_compressor_rocksdb;execution_path=sw;",
      &compressor);
  ASSERT_TRUE(s.ok()) << s.ToString();

  CompressionInfo compr_info(CompressionDict::GetEmptyDict());
  std::string compressed;
  Slice data(input, input_length);
  s = compressor->Compress(compr_info, data, &compressed);
  ASSERT_TRUE(s.ok()) << s.ToString();

  char* uncompressed;
  size_t uncompressed_length;
  UncompressionInfo uncompr_info_default_allocator(
      UncompressionDict::GetEmptyDict(), 2);
  s = compressor->Uncompress(uncompr_info_default_allocator, compressed.c_str(),
                             compressed.length(), &uncompressed,
                             &uncompressed_length);
  ASSERT_TRUE(s.ok()) << s.ToString();
  delete[] uncompressed;

  UncompressionInfo uncompr_info_null_allocator(
      UncompressionDict::GetEmptyDict(), 2, new NullMemoryAllocator());
  s = compressor->Uncompress(uncompr_info_null_allocator, compressed.c_str(),
                             compressed.length(), &uncompressed,
                             &uncompressed_length);
  ASSERT_TRUE(s.IsCorruption());
  ASSERT_EQ(s.ToString(), "Corruption: memory allocation error")
      << s.ToString();

  UncompressionInfo uncompr_info_exception_allocator(
      UncompressionDict::GetEmptyDict(), 2, new ExceptionMemoryAllocator());
  s = compressor->Uncompress(uncompr_info_exception_allocator,
                             compressed.c_str(), compressed.length(),
                             &uncompressed, &uncompressed_length);
  ASSERT_TRUE(s.IsCorruption());
  ASSERT_EQ(s.ToString(), "Corruption: memory allocation error")
      << s.ToString();
  ;

  DestroyBlock(input);
}

TEST(ErrorConditions, CompressEmptyInput) {
  std::shared_ptr<Compressor> compressor;
  ConfigOptions config_options;
  Status s = Compressor::CreateFromString(
      config_options, "id=com.intel.iaa_compressor_rocksdb;execution_path=sw;",
      &compressor);
  ASSERT_TRUE(s.ok());

  CompressionInfo compr_info(CompressionDict::GetEmptyDict());
  std::string compressed;
  Slice data(nullptr, 0);
  s = compressor->Compress(compr_info, data, &compressed);
  ASSERT_TRUE(s.IsCorruption());
  ASSERT_EQ(s.ToString(), "Corruption: QPL status 50") << s.ToString();
}

TEST(ErrorConditions, UncompressWrongSize) {
  size_t input_length = 1024;
  char* input = GenerateBlock(input_length);
  ASSERT_NE(input, nullptr);

  std::shared_ptr<Compressor> compressor;
  ConfigOptions config_options;
  Status s = Compressor::CreateFromString(
      config_options, "id=com.intel.iaa_compressor_rocksdb;execution_path=sw;",
      &compressor);
  ASSERT_TRUE(s.ok());

  CompressionInfo compr_info(CompressionDict::GetEmptyDict());
  std::string compressed;
  Slice data(input, input_length);
  s = compressor->Compress(compr_info, data, &compressed);
  ASSERT_TRUE(s.ok()) << s.ToString();

  char* uncompressed;
  size_t uncompressed_length;
  UncompressionInfo uncompr_info(UncompressionDict::GetEmptyDict());
  s = compressor->Uncompress(uncompr_info, compressed.c_str(),
                             compressed.length(), &uncompressed,
                             &uncompressed_length);
  ASSERT_TRUE(s.ok());
  delete[] uncompressed;

  s = compressor->Uncompress(uncompr_info, compressed.c_str(), 0, &uncompressed,
                             &uncompressed_length);
  ASSERT_TRUE(s.IsCorruption());
  ASSERT_EQ(s.ToString(), "Corruption: size decoding error");

  s = compressor->Uncompress(uncompr_info, compressed.c_str(), 10,
                             &uncompressed, &uncompressed_length);
  ASSERT_TRUE(s.IsCorruption());
  ASSERT_EQ(s.ToString(), "Corruption: size mismatch");
  delete[] uncompressed;

  // Overwrite uncompressed size in first 4 bytes
  for (int i = 0; i < 4; i++) {
    compressed[i] = 0;
  }
  s = compressor->Uncompress(uncompr_info, compressed.c_str(),
                             compressed.length(), &uncompressed,
                             &uncompressed_length);
  ASSERT_TRUE(s.IsCorruption());
  ASSERT_EQ(s.ToString(), "Corruption: QPL status 214");

  DestroyBlock(input);
}

struct TestParam {
  TestParam(std::string _execution_path, std::string _compression_mode,
            std::string _other_opts, size_t _block_size,
            unsigned int _num_blocks = 1)
      : execution_path(_execution_path),
        compression_mode(_compression_mode),
        other_opts(_other_opts),
        block_size(_block_size),
        num_blocks(_num_blocks) {}

  std::string execution_path;
  std::string compression_mode;
  std::string other_opts;
  size_t block_size;
  unsigned int num_blocks;

  std::string GetOpts() {
    return "execution_path=" + execution_path +
           ";compression_mode=" + compression_mode + ";" + other_opts;
  }
};

class IAACompressorTest
    : public testing::TestWithParam<std::tuple<
          std::string, std::string, std::string, size_t, unsigned int>> {
 public:
  static void SetUpTestSuite() {
    ObjectLibrary::Default()->AddFactory<Compressor>(
        "com.intel.iaa_compressor_rocksdb",
        [](const std::string& /* uri */, std::unique_ptr<Compressor>* c,
           std::string* /* errmsg */) {
          c->reset(NewIAACompressor().get());
          return c->get();
        });
  }

  void SetUp() override {
    TestParam test_param(std::get<0>(GetParam()), std::get<1>(GetParam()),
                         std::get<2>(GetParam()), std::get<3>(GetParam()),
                         std::get<4>(GetParam()));
    ;
    ConfigOptions config_options;
    Compressor::CreateFromString(
        config_options,
        "id=com.intel.iaa_compressor_rocksdb;" + test_param.GetOpts(),
        &compressor);
  }

  std::shared_ptr<Compressor> compressor;
};

TEST_P(IAACompressorTest, CompressDecompress) {
  TestParam test_param(std::get<0>(GetParam()), std::get<1>(GetParam()),
                       std::get<2>(GetParam()), std::get<3>(GetParam()),
                       std::get<4>(GetParam()));

  size_t input_length = test_param.block_size;
  char* input = GenerateBlock(input_length);
  ASSERT_NE(input, nullptr);

  CompressionInfo compr_info(CompressionDict::GetEmptyDict());
  std::string compressed;
  Slice data(input, input_length);
  Status s = compressor->Compress(compr_info, data, &compressed);
  ASSERT_TRUE(s.ok()) << s.ToString();

  UncompressionInfo uncompr_info(UncompressionDict::GetEmptyDict());
  char* uncompressed;
  size_t uncompressed_length;
  s = compressor->Uncompress(uncompr_info, compressed.c_str(),
                             compressed.length(), &uncompressed,
                             &uncompressed_length);
  ASSERT_TRUE(s.ok()) << s.ToString();
  ASSERT_EQ(uncompressed_length, input_length);
  ASSERT_TRUE(memcmp(uncompressed, input, input_length) == 0);
  delete[] uncompressed;

  DestroyBlock(input);
}

#define BLOCK_SIZES                                                       \
  100, 1 << 8, 1000, 1 << 10, 1 << 12, 1 << 14, 1 << 16, 100000, 1000000, \
      1 << 20

INSTANTIATE_TEST_SUITE_P(CompressSWDecompressSW, IAACompressorTest,
                         testing::Combine(testing::Values("sw"),
                                          testing::Values("dynamic", "fixed"),
                                          testing::Values("level=0", "level=1"),
                                          testing::Values(BLOCK_SIZES),
                                          testing::Values(1)));

#ifndef EXCLUDE_HW_TESTS
INSTANTIATE_TEST_SUITE_P(
    CompressHWDecompressHW, IAACompressorTest,
    testing::Combine(testing::Values("hw"), testing::Values("dynamic", "fixed"),
                     testing::Values("verify=false", "verify=true"),
                     testing::Values(BLOCK_SIZES), testing::Values(1)));

INSTANTIATE_TEST_SUITE_P(CompressSWDecompressHW, IAACompressorTest,
                         testing::Combine(testing::Values("hw"),
                                          testing::Values("dynamic"),
                                          testing::Values("level=1"),
                                          testing::Values(BLOCK_SIZES),
                                          testing::Values(1)));
#endif  // EXCLUDE_HW_TESTS

}  // namespace ROCKSDB_NAMESPACE

int main(int argc, char* argv[]) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
