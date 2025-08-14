#include "order_reader.h"
#include <arrow/api.h>
#include <arrow/parquet/reader.h>
#include <algorithm>
#include <stdexcept>

using namespace arrow;
using namespace parquet;

std::vector<Order> OrderReader::parse_parquet(const std::string& data) {
    std::vector<Order> orders;
    if (data.empty()) return orders;

    // 将字符串数据转换为输入流
    auto input = std::make_shared<io::BufferReader>(
        reinterpret_cast<const uint8_t*>(data.data()), data.size());

    // 打开Parquet文件
    auto reader = ParquetFileReader::Open(input);
    if (!reader) throw std::runtime_error("Failed to open parquet file");

    // 读取所有行组
    for (int i = 0; i < reader->num_row_groups(); ++i) {
        auto row_group = reader->RowGroup(i);
        
        // 读取各列数据
        auto skey_col = std::static_pointer_cast<Int32Array>(
            row_group->ReadColumnByName("skey").ValueOrDie());
        auto ts_col = std::static_pointer_cast<Int64Array>(
            row_group->ReadColumnByName("timestamp").ValueOrDie());
        auto bp_col = std::static_pointer_cast<Int32Array>(
            row_group->ReadColumnByName("bidPrice").ValueOrDie());
        auto bs_col = std::static_pointer_cast<Int32Array>(
            row_group->ReadColumnByName("bidSize").ValueOrDie());
        auto ap_col = std::static_pointer_cast<Int32Array>(
            row_group->ReadColumnByName("askPrice").ValueOrDie());
        auto as_col = std::static_pointer_cast<Int32Array>(
            row_group->ReadColumnByName("askSize").ValueOrDie());

        // 转换为Order对象
        for (int j = 0; j < skey_col->length(); ++j) {
            orders.push_back({
                skey_col->Value(j),
                ts_col->Value(j),
                bp_col->Value(j),
                bs_col->Value(j),
                ap_col->Value(j),
                as_col->Value(j)
            });
        }
    }

    return orders;
}

void OrderReader::load_all_skeys() {
    skey_data_.reserve(skey_list_.size());
    skey_indices_.reserve(skey_list_.size());

    // 并行加载每个skey的数据
    std::vector<std::future<std::vector<Order>>> futures;
    for (int skey : skey_list_) {
        futures.emplace_back(std::async(std::launch::async, [this, skey]() {
            std::string key = date_ + "/" + std::to_string(skey);
            std::string parquet_data;
            if (!kv_store_->get(key, parquet_data)) {
                throw std::runtime_error("Failed to load skey: " + std::to_string(skey));
            }
            return parse_parquet(parquet_data);
        }));
    }

    // 收集结果并初始化归并队列
    for (size_t i = 0; i < futures.size(); ++i) {
        auto data = futures[i].get();
        if (data.empty()) continue;

        // 确保数据有序（按timestamp）
        std::sort(data.begin(), data.end(),
                  [](const Order& a, const Order& b) { return a.timestamp < b.timestamp; });

        skey_data_.push_back(std::move(data));
        skey_indices_.push_back(0);

        // 将第一个元素加入归并队列
        merge_queue_.push(skey_data_.back()[0]);
    }
}

OrderReader::OrderReader(std::shared_ptr<IKVStore> kvStore,
                         const std::string& date,
                         const std::vector<int>& skeyList)
    : kv_store_(std::move(kvStore)), date_(date), skey_list_(skeyList) {
    load_all_skeys();
}

std::optional<Order> OrderReader::nextOrder() {
    if (merge_queue_.empty()) return std::nullopt;

    // 取出当前最小元素
    Order current = merge_queue_.top();
    merge_queue_.pop();

    // 找到对应的skey并补充下一个元素
    for (size_t i = 0; i < skey_data_.size(); ++i) {
        if (skey_data_[i].empty()) continue;
        if (skey_indices_[i] >= skey_data_[i].size()) continue;
        
        // 检查是否是当前skey的下一个元素
        if (skey_data_[i][skey_indices_[i]-1].skey == current.skey) {
            if (skey_indices_[i] < skey_data_[i].size()) {
                merge_queue_.push(skey_data_[i][skey_indices_[i]]);
                skey_indices_[i]++;
            }
            break;
        }
    }

    return current;
}
