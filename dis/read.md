

### 优化说明及实现亮点

1. **统一LRU缓存管理**
   - 设计了`UnifiedLRUCache`类，同时管理内存和磁盘缓存，使用单一LRU策略决定数据存放位置
   - 当内存不足时，自动将最久未使用的内存数据写入磁盘；当磁盘也满时，删除最久未使用的磁盘数据
   - 通过`in_memory`标记区分数据存储位置，实现透明的缓存访问

2. **单元测试设计**
   - 使用Google Test框架，覆盖核心场景：正常归并、空数据、单个skey
   - 通过`MockKVStore`模拟S3数据读取，避免测试依赖真实S3服务
   - 验证归并结果的时间顺序正确性，确保多路归并逻辑无误

3. **基准测试支持**
   - 使用Google Benchmark框架，测量关键性能指标：
     - 吞吐量：单位时间内处理的订单数量（订单/秒）
     - 内存使用：不同数据量下的内存占用
   - 支持参数化测试，可模拟不同skey数量和订单规模的场景


### 编译与运行建议

1. **依赖安装**
   ```bash
   # 安装AWS SDK、Arrow、Google Test和Benchmark
   sudo apt-get install libaws-cpp-sdk-s3-dev libarrow-dev libparquet-dev
   sudo apt-get install libgtest-dev libbenchmark-dev
   ```

2. **编译测试**
   ```bash
   # 编译单元测试
   g++ order_reader_test.cpp order_reader.cpp dos_kv_client.cpp -o order_test \
       -lgtest -lgtest_main -laws-cpp-sdk-s3 -larrow -lparquet -pthread
   
   # 编译基准测试
   g++ order_reader_benchmark.cpp order_reader.cpp dos_kv_client.cpp -o order_bench \
       -lbenchmark -lgtest -laws-cpp-sdk-s3 -larrow -lparquet -pthread
   ```

3. **运行**
   ```bash
   # 运行单元测试
   ./order_test
   
   # 运行基准测试
   ./order_bench --benchmark_format=csv > benchmark_results.csv
   ```

通过这些实现，不仅满足了功能需求，还确保了代码的正确性和性能可衡量性，为后续优化提供了坚实基础。