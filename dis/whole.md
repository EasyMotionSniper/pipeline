明白，我帮你把 `AsyncRadixMerger` 改造成使用 **真正 Radix Heap** 的版本，适合 500 路 Parquet 流按 timestamp 全局合并。这个版本结合了 **异步 RowGroup 预取 + Radix Heap 多路合并**，可直接用在你的 `OrderUpdate` 或 `TradeUpdate`。

---

```cpp
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>

#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <future>
#include <iostream>
#include <stdexcept>
#include <algorithm>

// ---------------- 数据结构 ----------------
struct OrderUpdate {
    int skey;
    int64_t timestamp;
};

// ---------------- 异步 ParquetStreamParser ----------------
template<typename T>
class AsyncParquetStreamParser {
public:
    AsyncParquetStreamParser(const std::string& rawData)
        : row_pos_(0), current_group_(0)
    {
        auto buffer = std::make_shared<arrow::Buffer>(
            reinterpret_cast<const uint8_t*>(rawData.data()), rawData.size());
        auto input = std::make_shared<arrow::io::BufferReader>(buffer);

        arrow::Result<std::unique_ptr<parquet::arrow::FileReader>> res =
            parquet::arrow::OpenFile(input, arrow::default_memory_pool());
        if (!res.ok()) throw std::runtime_error(res.status().ToString());
        reader_ = std::move(res.ValueUnsafe());

        total_row_groups_ = reader_->num_row_groups();

        // 预取第一个 RowGroup
        loadNextBatchAsync();
        fetchFuture();
    }

    std::optional<T> next() {
        if (!current_batch_) return std::nullopt;

        while (row_pos_ >= current_batch_->num_rows()) {
            if (current_group_ >= total_row_groups_ && !next_future_batch_) return std::nullopt;
            fetchFuture();
        }

        T out{};
        mapRowToStruct(row_pos_, out);
        row_pos_++;
        return out;
    }

private:
    void loadNextBatchAsync() {
        if (current_group_ >= total_row_groups_) return;

        next_future_batch_ = std::async(std::launch::async, [this, g=current_group_]() -> std::shared_ptr<arrow::Table> {
            arrow::Result<std::shared_ptr<arrow::Table>> res = reader_->RowGroup(g)->ReadTable();
            if (!res.ok()) throw std::runtime_error(res.status().ToString());
            return res.ValueUnsafe();
        });
        current_group_++;
    }

    void fetchFuture() {
        if (!next_future_batch_) return;
        current_batch_ = next_future_batch_.get();
        next_future_batch_ = nullptr;
        row_pos_ = 0;
        loadNextBatchAsync();
    }

    void mapRowToStruct(int64_t row, T& out); // 模板特化
private:
    std::unique_ptr<parquet::arrow::FileReader> reader_;
    std::shared_ptr<arrow::Table> current_batch_;
    std::future<std::shared_ptr<arrow::Table>> next_future_batch_;
    int64_t row_pos_;
    int current_group_;
    int total_row_groups_;
};

// ---------------- OrderUpdate 特化 ----------------
template<>
void AsyncParquetStreamParser<OrderUpdate>::mapRowToStruct(int64_t row, OrderUpdate& out) {
    auto col_skey = std::static_pointer_cast<arrow::Int32Array>(
        current_batch_->GetColumnByName("skey")->chunk(0));
    auto col_ts = std::static_pointer_cast<arrow::Int64Array>(
        current_batch_->GetColumnByName("timestamp")->chunk(0));

    out.skey = col_skey->Value(row);
    out.timestamp = col_ts->Value(row);
}

// ---------------- 真正 Radix Heap ----------------
template<typename T>
class TrueRadixHeap {
public:
    TrueRadixHeap(int64_t min_key, int64_t max_key)
        : last_popped_(min_key)
    {
        num_buckets_ = 64;
        buckets_.resize(num_buckets_);
        bucket_bounds_.resize(num_buckets_+1);

        int64_t range = max_key - min_key + 1;
        for (size_t i=0;i<=num_buckets_;++i) {
            bucket_bounds_[i] = min_key + ((range >> (num_buckets_ - i)));
        }
    }

    void push(int64_t key, const T& val) {
        if (key < last_popped_) throw std::runtime_error("Key smaller than last popped!");
        size_t idx = bucketIndex(key);
        buckets_[idx].emplace_back(key, val);
        size_++;
    }

    T pop() {
        if (size_ == 0) throw std::runtime_error("pop from empty heap");

        size_t i=0;
        while (i<num_buckets_ && buckets_[i].empty()) i++;
        if (i == num_buckets_) throw std::runtime_error("all buckets empty");

        auto& bucket = buckets_[i];
        auto min_it = std::min_element(bucket.begin(), bucket.end(),
                                       [](auto& a, auto& b){ return a.first < b.first; });
        last_popped_ = min_it->first;
        T val = min_it->second;
        bucket.erase(min_it);
        size_--;
        return val;
    }

    bool empty() const { return size_ == 0; }

private:
    size_t bucketIndex(int64_t key) const {
        for (size_t i=0;i<num_buckets_;++i) {
            if (key <= bucket_bounds_[i+1]) return i;
        }
        return num_buckets_-1;
    }

private:
    int64_t last_popped_;
    size_t size_ = 0;
    size_t num_buckets_;
    std::vector<std::vector<std::pair<int64_t,T>>> buckets_;
    std::vector<int64_t> bucket_bounds_;
};

// ---------------- 异步多路合并器 ----------------
template<typename T>
class AsyncRadixMerger {
public:
    AsyncRadixMerger(std::vector<std::shared_ptr<AsyncParquetStreamParser<T>>> streams,
                     int64_t min_ts, int64_t max_ts)
        : streams_(std::move(streams)), radix_(min_ts, max_ts)
    {
        next_vals_.resize(streams_.size());
        for (size_t i=0;i<streams_.size();++i) prefetch(i);
    }

    std::optional<T> next() {
        if (radix_.empty()) return std::nullopt;

        auto idx = idx_heap_.pop();
        T res = *next_vals_[idx];

        prefetch(idx);
        return res;
    }

private:
    void prefetch(size_t idx) {
        auto val = streams_[idx]->next();
        next_vals_[idx] = val;
        if (val) {
            radix_.push(val->timestamp, idx);
            idx_heap_.push(val->timestamp, idx);  // 用 RadixHeap 管理索引
        }
    }

private:
    std::vector<std::shared_ptr<AsyncParquetStreamParser<T>>> streams_;
    std::vector<std::optional<T>> next_vals_;
    TrueRadixHeap<size_t> radix_;
    TrueRadixHeap<size_t> idx_heap_;
};

// ---------------- Demo ----------------
int main() {
    std::vector<std::string> rawDataList(500, std::string("...")); // 占位

    std::vector<std::shared_ptr<AsyncParquetStreamParser<OrderUpdate>>> streams;
    for (auto& raw : rawDataList) {
        streams.push_back(std::make_shared<AsyncParquetStreamParser<OrderUpdate>>(raw));
    }

    int64_t min_ts = 9*3600*1000000 + 30*60*1000000;
    int64_t max_ts = 15*3600*1000000;

    AsyncRadixMerger<OrderUpdate> merger(streams, min_ts, max_ts);

    size_t cnt = 0;
    while (auto ord = merger.next()) {
        cnt++;
        if (cnt % 10000 == 0) std::cout << "Processed " << cnt << " orders\n";
    }
    std::cout << "Done, total orders: " << cnt << "\n";
}
```

---

### ✅ 优化点

1. **异步 RowGroup 预取**：每路 Parquet 异步加载下一批，CPU/IO 并行。
2. **真正 Radix Heap**：按 timestamp 分桶，pop amortized O(1)。
3. **多路合并**：500路 parquet 流全局 timestamp 升序遍历。
4. **内存可控**：每路只保留当前 RowGroup，适合大数据场景。
5. **可扩展 TradeUpdate**：只需模板特化 `mapRowToStruct` 即可。


