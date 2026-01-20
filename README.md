# Intel&reg; In-Memory Analytics Accelerator Plugin for RocksDB Storage Engine
The Intel&reg; In-Memory Analytics Accelerator (IAA) plugin for RocksDB provides accelerated compression/decompression in RocksDB using IAA and [QPL](https://github.com/intel/qpl) (Query Processing Library). It is dependent on the pluggable compression framework offered in [PR6717](https://github.com/facebook/rocksdb/pull/6717) that is subject to change. Please use the latest release of this plugin to ensure compatibility with the latest content of the pluggable compression PR.

For more information about the Intel&reg; In-Memory Analytics Accelerator, refer to the [IAA spec](https://cdrdv2.intel.com/v1/dl/getContent/721858) on the [Intel&reg; 64 and IA-32 Architectures Software Developer Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) page.
For more information about plugin support in RocksDB, refer to the [instructions](https://github.com/facebook/rocksdb/tree/main/plugin) in RocksDB and [PR 7918](https://github.com/facebook/rocksdb/pull/7918).


# Building RocksDB with the IAA Plugin

- Install QPL. Follow the instructions in QPL's [readme](https://github.com/intel/qpl). The IAA plugin was tested with QPL [v1.7.0](https://github.com/intel/qpl/releases/tag/v1.7.0). Note that to access the hardware path and configure IAA, kernel 5.18 and accel-config are required, as described in QPL's [system requirements](https://intel.github.io/qpl/documentation/get_started_docs/system_requirements.html). The plugin requires shared workqueues to be configured with block_on_fault enabled.

- Clone RocksDB with pluggable compression support in [PR6717](https://github.com/facebook/rocksdb/pull/6717)

```
git clone --branch pluggable_compression https://github.com/lucagiac81/rocksdb.git
cd rocksdb
```

- Clone the IAA plugin in the plugin directory in RocksDB

```
git clone https://github.com/intel/iaa-plugin-rocksdb.git plugin/iaa_compressor
```

### Build with make

```
ROCKSDB_PLUGINS="iaa_compressor" make -j release
```

If QPL was not installed in a default directory, add EXTRA_CXXFLAGS and EXTRA_LDFLAGS. For example

```
EXTRA_CXXFLAGS="-I<qpl_install_directory>/include" EXTRA_LDFLAGS="-L<qpl_install_directory>/lib64" ROCKSDB_PLUGINS="iaa_compressor" make -j release
```

### Build with CMake

```
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DROCKSDB_PLUGINS="iaa_compressor"
make -j
```

If QPL was not installed in a default directory, add CXXFLAGS and LDFLAGS in the cmake command

```
CXXFLAGS="-I/<qpl_install_directory>/include" LDFLAGS="-L<qpl_install_directory>lib64" cmake .. -DROCKSDB_PLUGINS="iaa_compressor"
```

### Verify Installation
To verify the installation, you can use db_bench and verify no errors are reported. The first command below verifies the software path, the second the hardware path.

```
./db_bench --benchmarks=fillseq --compression_type=com.intel.iaa_compressor_rocksdb --compressor_options="execution_path=sw"
./db_bench --benchmarks=fillseq --compression_type=com.intel.iaa_compressor_rocksdb --compressor_options="execution_path=hw"
```

# Testing

- Install QPL, RocksDB with pluggable compression support, and IAA plugin as described in the previous section.
- Install [googletest](https://github.com/google/googletest)
- Build RocksDB as a shared library

```
LIB_MODE=shared make -j release
```

- Go to the tests directory of the IAA plugin and build the tests with CMake

```
cd plugin/iaa_compressor/tests
mkdir build
cd build
cmake ..
make run
```

If QPL and RocksDB were not installed in default directories, the path can be specified as follows

```
cmake -DROCKSDB_PATH=<rocksdb_install_directory> -DQPL_PATH=<qpl_install_directory> ..
```

To run only tests using the QPL software path (not using the IAA hardware), use the option -DEXCLUDE_HW_TESTS=ON.

# Using the Plugin

To use the IAA plugin for compression/decompression, select it as compression type (com.intel.iaa_compressor_rocksdb) just like any other algorithm. Refer to the examples in [PR6717](https://github.com/facebook/rocksdb/pull/6717). The reverse domain naming convention was selected to avoid conflicts in the future as more plugins are available. 

In the following examples, execution_path is used as an example of compressor options. You can use any combination of supported options (refer to the Compressor Options section) in a semicolon-separated list.

To configure RocksDB using an option string

```
compressor={id=com.intel.iaa_compressor_rocksdb;execution_path=hw}
```

To configure RocksDB using an Options object

```
Options options;
ConfigOptions config_options;
  Status s = Compressor::CreateFromString(
      config_options,
      "id=com.intel.iaa_compressor_rocksdb;execution_path=hw",
      &options.compressor);
```

To select in db_bench

```
./db_bench --compression_type=com.intel.iaa_compressor_rocksdb  --compressor_options="execution_path=hw"
```

# Compressor Options

The compressor offers several options:
- execution_path
  - "auto" (default): QPL decides whether to use the hardware or software path.
  - "hw": QPL will use the IAA hardware.
  - "sw": QPL will run compression/decompression in software.
- compression_mode
  - "dynamic" (default): for compression, a Huffman table is computed each time (requires two passes over the data, but provides, in general, better compression ratio).
  - "fixed": a predefined Huffman table is used.
- verify
  - "true": run verification for compression (decompress and verify data matches original).
  - "false" (default): skip verification.
- level
  - "0" or kDefaultCompressionLevel (default): default compression level (supported by hardware and software path).
  - otherwise: high compression level (supported only by software path).
- parallel_threads: refer to the parallel_threads option in RocksDB. Default = 1.
