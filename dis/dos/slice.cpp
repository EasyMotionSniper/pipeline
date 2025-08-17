// src/slicer/data_slicer.cpp
#include "slicer/data_slicer.h"
#include <algorithm>

template<typename T>
std::vector<DataSlicer::Slice> DataSlicer::createSlices(const std::vector<T>& data) {
    std::vector<Slice> slices;
    if (data.empty()) return slices;
    
    size_t elementSize = sizeof(T);
    size_t elementsPerSlice = SLICE_SIZE / elementSize;
    
    for (size_t i = 0; i < data.size(); i += elementsPerSlice) {
        Slice slice;
        slice.startIndex = i;
        slice.endIndex = std::min(i + elementsPerSlice, data.size());
        slice.minTimestamp = data[slice.startIndex].timestamp;
        slice.maxTimestamp = data[slice.endIndex - 1].timestamp;
        slices.push_back(slice);
    }
    
    return slices;
}

template<typename T>
std::vector<T> DataSlicer::extractSlice(const std::vector<T>& data, const Slice& slice) {
    return std::vector<T>(data.begin() + slice.startIndex, 
                         data.begin() + slice.endIndex);
}

// Explicit instantiations
template std::vector<DataSlicer::Slice> DataSlicer::createSlices<OrderUpdate>(const std::vector<OrderUpdate>&);
template std::vector<OrderUpdate> DataSlicer::extractSlice<OrderUpdate>(const std::vector<OrderUpdate>&, const Slice&);
template std::vector<DataSlicer::Slice> DataSlicer::createSlices<TradeUpdate>(const std::vector<TradeUpdate>&);
template std::vector<TradeUpdate> DataSlicer::extractSlice<TradeUpdate>(const std::vector<TradeUpdate>&, const Slice&);

// src/reader/sliced_order_reader.cpp
#include "reader/sliced_order_reader.h"
#include "reader/parquet_parser.h"
#include <fmt/format.h>

class SlicedOrderReader : public OrderReader {
private:
    struct SlicedStream {
        int skey;
        std::vector<DataSlicer::Slice> slices;
        size_t currentSliceIndex;
        std::vector<OrderUpdate> currentData;
        size_t currentIndex;
        std::shared_ptr<IKVStore> kvStore;
        int date;
        
        bool needsRefill() const {
            return currentIndex >= currentData.size();
        }
        
        bool hasMoreSlices() const {
            return currentSliceIndex < slices.size();
        }
        
        void loadNextSlice() {
            if (!hasMoreSlices()) {
                currentData.clear();
                return;
            }
            
            // Create slice key
            std::string key = fmt::format("mdl/mbd/order/{}/{}-{}.parquet", 
                                        date, skey, currentSliceIndex);
            std::string value;
            kvStore->get(key, value);
            
            // Parse only this slice
            auto allData = ParquetParser::parseOrderData(value);
            currentData = DataSlicer::extractSlice(allData, slices[currentSliceIndex]);
            currentIndex = 0;
            currentSliceIndex++;
        }
    };
    
    std::vector<std::unique_ptr<SlicedStream>> slicedStreams;
    std::priority_queue<std::pair<OrderUpdate, int>,
                       std::vector<std::pair<OrderUpdate, int>>,
                       std::greater<>> minHeap;
    
public:
    SlicedOrderReader(std::shared_ptr<IKVStore> kvStore, int date,
                     const std::vector<int>& skeyList)
        : OrderReader(kvStore, date, skeyList) {
        
        // Initialize sliced streams
        for (int skey : skeyList) {
            auto stream = std::make_unique<SlicedStream>();
            stream->skey = skey;
            stream->currentSliceIndex = 0;
            stream->currentIndex = 0;
            stream->kvStore = kvStore;
            stream->date = date;
            
            // Load metadata to get slice information
            loadSliceMetadata(stream.get());
            
            // Load first slice
            stream->loadNextSlice();
            
            slicedStreams.push_back(std::move(stream));
            
            // Add to heap if has data
            if (!slicedStreams.back()->currentData.empty()) {
                minHeap.push({slicedStreams.back()->currentData[0], 
                            static_cast<int>(slicedStreams.size() - 1)});
            }
        }
    }
    
    std::optional<OrderUpdate> nextOrder() override {
        if (minHeap.empty()) {
            return std::nullopt;
        }
        
        auto [order, streamIndex] = minHeap.top();
        minHeap.pop();
        
        // Advance stream
        auto& stream = slicedStreams[streamIndex];
        stream->currentIndex++;
        
        // Check if needs refill
        if (stream->needsRefill() && stream->hasMoreSlices()) {
            stream->loadNextSlice();
        }
        
        // Add back to heap if has more data
        if (stream->currentIndex < stream->currentData.size()) {
            minHeap.push({stream->currentData[stream->currentIndex], streamIndex});
        }
        
        return order;
    }
    
private:
    void loadSliceMetadata(SlicedStream* stream) {
        // In real implementation, this would read slice metadata
        // For now, create dummy slices
        DataSlicer::Slice slice;
        slice.startIndex = 0;
        slice.endIndex = 1000;
        slice.minTimestamp = 0;
        slice.maxTimestamp = INT64_MAX;
        stream->slices.push_back(slice);
    }
};

// src/cache/optimized_cache.cpp
#include "cache/optimized_cache.h"
#include <zlib.h>
#include <thread>
#include <chrono>
#include <msgpack.hpp>

class OptimizedCache : public CacheManager {
private:
    struct ColumnStore {
        std::vector<int> skeys;
        std::vector<int64_t> timestamps;
        std::vector<int> bidPrices;
        std::vector<int> bidSizes;
        std::vector<int> askPrices;
        std::vector<int> askSizes;
        std::vector<char> sides;
        
        size_t size() const {
            return skeys.size();
        }
        
        void reserve(size_t n) {
            skeys.reserve(n);
            timestamps.reserve(n);
            bidPrices.reserve(n);
            bidSizes.reserve(n);
            askPrices.reserve(n);
            askSizes.reserve(n);
            sides.reserve(n);
        }
        
        void add(const OrderUpdate& order) {
            skeys.push_back(order.skey);
            timestamps.push_back(order.timestamp);
            bidPrices.push_back(order.bidPrice);
            bidSizes.push_back(order.bidSize);
            askPrices.push_back(order.askPrice);
            askSizes.push_back(order.askSize);
            sides.push_back(order.side);
        }
        
        OrderUpdate get(size_t index) const {
            OrderUpdate order;
            order.skey = skeys[index];
            order.timestamp = timestamps[index];
            order.bidPrice = bidPrices[index];
            order.bidSize = bidSizes[index];
            order.askPrice = askPrices[index];
            order.askSize = askSizes[index];
            order.side = sides[index];
            return order;
        }
    };
    
    class BatchLoader {
    private:
        static constexpr size_t BATCH_SIZE = 100;
        std::unordered_map<int, std::vector<OrderUpdate>> batchCache;
        std::mutex batchMutex;
        
    public:
        void prefetch(std::shared_ptr<IKVStore> kvStore, 
                     int date,
                     const std::vector<int>& skeys) {
            std::vector<std::future<void>> futures;
            
            for (int skey : skeys) {
                futures.push_back(std::async(std::launch::async, [this, kvStore, date, skey]() {
                    std::string key = fmt::format("mdl/mbd/order/{}/{}.parquet", date, skey);
                    std::string value;
                    kvStore->get(key, value);
                    
                    auto orders = ParquetParser::parseOrderData(value);
                    
                    std::lock_guard<std::mutex> lock(batchMutex);
                    batchCache[skey] = std::move(orders);
                }));
            }
            
            // Wait for all prefetches to complete
            for (auto& f : futures) {
                f.wait();
            }
        }
        
        std::vector<OrderUpdate> getBatch(int skey) {
            std::lock_guard<std::mutex> lock(batchMutex);
            auto it = batchCache.find(skey);
            if (it != batchCache.end()) {
                return std::move(it->second);
            }
            return {};
        }
    };
    
    BatchLoader batchLoader;
    std::thread prefetchThread;
    std::atomic<bool> stopPrefetch{false};
    
    void compressData(const std::string& input, std::string& output) {
        z_stream zs;
        memset(&zs, 0, sizeof(zs));
        
        if (deflateInit(&zs, Z_BEST_COMPRESSION) != Z_OK) {
            throw std::runtime_error("deflateInit failed");
        }
        
        zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
        zs.avail_in = input.size();
        
        int ret;
        char outbuffer[32768];
        std::string outstring;
        
        do {
            zs.next_out = reinterpret_cast<Bytef*>(outbuffer);
            zs.avail_out = sizeof(outbuffer);
            
            ret = deflate(&zs, Z_FINISH);
            
            if (outstring.size() < zs.total_out) {
                outstring.append(outbuffer, zs.total_out - outstring.size());
            }
        } while (ret == Z_OK);
        
        deflateEnd(&zs);
        
        if (ret != Z_STREAM_END) {
            throw std::runtime_error("Exception during zlib compression");
        }
        
        output = outstring;
    }
    
    void decompressData(const std::string& input, std::string& output) {
        z_stream zs;
        memset(&zs, 0, sizeof(zs));
        
        if (inflateInit(&zs) != Z_OK) {
            throw std::runtime_error("inflateInit failed");
        }
        
        zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
        zs.avail_in = input.size();
        
        int ret;
        char outbuffer[32768];
        std::string outstring;
        
        do {
            zs.next_out = reinterpret_cast<Bytef*>(outbuffer);
            zs.avail_out = sizeof(outbuffer);
            
            ret = inflate(&zs, 0);
            
            if (outstring.size() < zs.total_out) {
                outstring.append(outbuffer, zs.total_out - outstring.size());
            }
        } while (ret == Z_OK);
        
        inflateEnd(&zs);
        
        if (ret != Z_STREAM_END) {
            throw std::runtime_error("Exception during zlib decompression");
        }
        
        output = outstring;
    }
    
public:
    OptimizedCache(const std::string& dir, size_t maxSize)
        : CacheManager(dir, maxSize) {
        
        // Start prefetch thread
        prefetchThread = std::thread([this]() {
            while (!stopPrefetch) {
                // Prefetch logic based on access patterns
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }
    
    ~OptimizedCache() {
        stopPrefetch = true;
        if (prefetchThread.joinable()) {
            prefetchThread.join();
        }
    }
    
    void putOptimized(const std::string& key, const ColumnStore& data) {
        // Serialize column store
        msgpack::sbuffer buffer;
        msgpack::pack(buffer, data.skeys);
        msgpack::pack(buffer, data.timestamps);
        msgpack::pack(buffer, data.bidPrices);
        msgpack::pack(buffer, data.bidSizes);
        msgpack::pack(buffer, data.askPrices);
        msgpack::pack(buffer, data.askSizes);
        msgpack::pack(buffer, data.sides);
        
        // Compress
        std::string compressed;
        compressData(std::string(buffer.data(), buffer.size()), compressed);
        
        // Store
        put(key, compressed);
    }
    
    bool getOptimized(const std::string& key, ColumnStore& data) {
        std::string compressed;
        if (!get(key, compressed)) {
            return false;
        }
        
        // Decompress
        std::string decompressed;
        decompressData(compressed, decompressed);
        
        // Deserialize
        msgpack::unpacker pac;
        pac.reserve_buffer(decompressed.size());
        memcpy(pac.buffer(), decompressed.data(), decompressed.size());
        pac.buffer_consumed(decompressed.size());
        
        msgpack::object_handle oh;
        pac.next(oh); oh.get().convert(data.skeys);
        pac.next(oh); oh.get().convert(data.timestamps);
        pac.next(oh); oh.get().convert(data.bidPrices);
        pac.next(oh); oh.get().convert(data.bidSizes);
        pac.next(oh); oh.get().convert(data.askPrices);
        pac.next(oh); oh.get().convert(data.askSizes);
        pac.next(oh); oh.get().convert(data.sides);
        
        return true;
    }
    
    void getBatch(const std::vector<std::string>& keys, 
                  std::vector<std::string>& values) {
        values.reserve(keys.size());
        
        // Parallel fetch
        std::vector<std::future<std::string>> futures;
        for (const auto& key : keys) {
            futures.push_back(std::async(std::launch::async, [this, &key]() {
                std::string value;
                get(key, value);
                return value;
            }));
        }
        
        for (auto& f : futures) {
            values.push_back(f.get());
        }
    }
};

// src/reader/optimized_order_reader.cpp
class OptimizedOrderReader : public OrderReader {
private:
    std::shared_ptr<OptimizedCache> cache;
    BatchLoader batchLoader;
    
    // Memory-mapped file for ultra-fast access
    class MMapReader {
    private:
        void* mappedMemory;
        size_t fileSize;
        int fd;
        
    public:
        MMapReader(const std::string& path) {
            fd = open(path.c_str(), O_RDONLY);
            if (fd == -1) {
                throw std::runtime_error("Failed to open file");
            }
            
            struct stat sb;
            if (fstat(fd, &sb) == -1) {
                close(fd);
                throw std::runtime_error("Failed to get file size");
            }
            
            fileSize = sb.st_size;
            mappedMemory = mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
            
            if (mappedMemory == MAP_FAILED) {
                close(fd);
                throw std::runtime_error("Failed to mmap file");
            }
            
            // Advise kernel about access pattern
            madvise(mappedMemory, fileSize, MADV_SEQUENTIAL);
        }
        
        ~MMapReader() {
            if (mappedMemory != MAP_FAILED) {
                munmap(mappedMemory, fileSize);
            }
            if (fd != -1) {
                close(fd);
            }
        }
        
        std::string_view getData() const {
            return std::string_view(static_cast<char*>(mappedMemory), fileSize);
        }
    };
    
public:
    OptimizedOrderReader(std::shared_ptr<IKVStore> kvStore, 
                        int date,
                        const std::vector<int>& skeyList)
        : OrderReader(kvStore, date, skeyList) {
        
        // Pre-fetch all data in parallel
        batchLoader.prefetch(kvStore, date, skeyList);
        
        // Initialize optimized cache
        cache = std::make_shared<OptimizedCache>("/tmp/opt_cache", 2ULL * 1024 * 1024 * 1024);
    }
};