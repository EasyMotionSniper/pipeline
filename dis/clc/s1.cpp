// src/s3_kv_store.cpp
#include "s3_kv_store.h"
#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <sstream>
#include <iostream>

// include/s3_kv_store.h
#pragma once
#include "ikv_store.h"
#include <aws/s3/S3Client.h>
#include <memory>

class S3KVStore : public IKVStore {
private:
    std::shared_ptr<Aws::S3::S3Client> s3_client_;
    std::string bucket_name_;
    std::string prefix_;
    
public:
    S3KVStore(const std::string& bucket, const std::string& prefix = "mdl/mbd/order/");
    virtual ~S3KVStore();
    
    void get(const std::string& key, std::string& value) override;
    void put(const std::string& key, const std::string& value) override;
    
    // Helper function to list all keys for a date
    std::vector<std::string> listKeys(const std::string& date_prefix);
};

// Implementation
S3KVStore::S3KVStore(const std::string& bucket, const std::string& prefix)
    : bucket_name_(bucket), prefix_(prefix) {
    Aws::Client::ClientConfiguration config;
    config.region = Aws::Region::US_EAST_1;
    s3_client_ = std::make_shared<Aws::S3::S3Client>(config);
}

S3KVStore::~S3KVStore() = default;

void S3KVStore::get(const std::string& key, std::string& value) {
    Aws::S3::Model::GetObjectRequest request;
    request.SetBucket(bucket_name_);
    request.SetKey(prefix_ + key);
    
    auto outcome = s3_client_->GetObject(request);
    if (!outcome.IsSuccess()) {
        throw std::runtime_error("Failed to get object: " + 
                               outcome.GetError().GetMessage());
    }
    
    auto& stream = outcome.GetResult().GetBody();
    std::ostringstream ss;
    ss << stream.rdbuf();
    value = ss.str();
}

void S3KVStore::put(const std::string& key, const std::string& value) {
    Aws::S3::Model::PutObjectRequest request;
    request.SetBucket(bucket_name_);
    request.SetKey(prefix_ + key);
    
    auto data = std::make_shared<Aws::StringStream>(value);
    request.SetBody(data);
    
    auto outcome = s3_client_->PutObject(request);
    if (!outcome.IsSuccess()) {
        throw std::runtime_error("Failed to put object: " + 
                               outcome.GetError().GetMessage());
    }
}

std::vector<std::string> S3KVStore::listKeys(const std::string& date_prefix) {
    std::vector<std::string> keys;
    Aws::S3::Model::ListObjectsV2Request request;
    request.SetBucket(bucket_name_);
    request.SetPrefix(prefix_ + date_prefix);
    
    bool done = false;
    while (!done) {
        auto outcome = s3_client_->ListObjectsV2(request);
        if (!outcome.IsSuccess()) {
            throw std::runtime_error("Failed to list objects: " + 
                                   outcome.GetError().GetMessage());
        }
        
        auto& contents = outcome.GetResult().GetContents();
        for (const auto& object : contents) {
            std::string key = object.GetKey();
            // Remove prefix to get relative key
            if (key.substr(0, prefix_.length()) == prefix_) {
                keys.push_back(key.substr(prefix_.length()));
            }
        }
        
        if (outcome.GetResult().GetIsTruncated()) {
            request.SetContinuationToken(outcome.GetResult().GetNextContinuationToken());
        } else {
            done = true;
        }
    }
    
    return keys;
}

// src/order_reader.cpp - Version 1
#include "order_reader.h"
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>
#include <parquet/arrow/writer.h>

OrderReader::OrderReader(std::shared_ptr<IKVStore> kvStore, int date,
                        const std::vector<int>& skeyList)
    : kv_store_(kvStore), date_(date) {
    
    // Load data for each skey
    for (int skey : skeyList) {
        loadOrderData(skey);
    }
    
    // Initialize priority queue with first order from each source
    for (size_t i = 0; i < sources_.size(); ++i) {
        if (sources_[i]->hasNext()) {
            pq_.push({sources_[i]->peek(), static_cast<int>(i)});
            sources_[i]->current_idx++;
        }
    }
}

void OrderReader::loadOrderData(int skey) {
    auto source = std::make_unique<OrderSource>(skey);
    
    // Construct key: {date}/{skey}.parquet
    std::string key = std::to_string(date_) + "/" + std::to_string(skey) + ".parquet";
    
    try {
        std::string parquet_data;
        kv_store_->get(key, parquet_data);
        source->orders = parseParquetData(parquet_data);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load data for skey " << skey << ": " << e.what() << std::endl;
        // Continue with empty source
    }
    
    sources_.push_back(std::move(source));
}

std::vector<OrderUpdate> OrderReader::parseParquetData(const std::string& data) {
    std::vector<OrderUpdate> orders;
    
    // Create Arrow buffer from string data
    auto buffer = arrow::Buffer::FromString(data);
    auto input = std::make_shared<arrow::io::BufferReader>(buffer);
    
    // Open Parquet file
    std::unique_ptr<parquet::arrow::FileReader> reader;
    auto status = parquet::arrow::OpenFile(input, arrow::default_memory_pool(), &reader);
    if (!status.ok()) {
        throw std::runtime_error("Failed to open parquet file: " + status.ToString());
    }
    
    // Read table
    std::shared_ptr<arrow::Table> table;
    status = reader->ReadTable(&table);
    if (!status.ok()) {
        throw std::runtime_error("Failed to read parquet table: " + status.ToString());
    }
    
    // Extract columns
    auto skey_array = std::static_pointer_cast<arrow::Int32Array>(
        table->column(0)->chunk(0));
    auto timestamp_array = std::static_pointer_cast<arrow::Int64Array>(
        table->column(1)->chunk(0));
    auto bid_price_array = std::static_pointer_cast<arrow::Int32Array>(
        table->column(2)->chunk(0));
    auto bid_size_array = std::static_pointer_cast<arrow::Int32Array>(
        table->column(3)->chunk(0));
    auto ask_price_array = std::static_pointer_cast<arrow::Int32Array>(
        table->column(4)->chunk(0));
    auto ask_size_array = std::static_pointer_cast<arrow::Int32Array>(
        table->column(5)->chunk(0));
    auto side_array = std::static_pointer_cast<arrow::StringArray>(
        table->column(6)->chunk(0));
    
    // Convert to OrderUpdate vector
    int64_t num_rows = table->num_rows();
    orders.reserve(num_rows);
    
    for (int64_t i = 0; i < num_rows; ++i) {
        OrderUpdate order;
        order.skey = skey_array->Value(i);
        order.timestamp = timestamp_array->Value(i);
        order.bidPrice = bid_price_array->Value(i);
        order.bidSize = bid_size_array->Value(i);
        order.askPrice = ask_price_array->Value(i);
        order.askSize = ask_size_array->Value(i);
        order.side = side_array->GetString(i)[0];
        orders.push_back(order);
    }
    
    return orders;
}

std::optional<Order> OrderReader::nextOrder() {
    if (pq_.empty()) {
        return std::nullopt;
    }
    
    // Get the order with minimum timestamp
    auto [order, source_idx] = pq_.top();
    pq_.pop();
    
    // Add next order from the same source if available
    if (sources_[source_idx]->hasNext()) {
        pq_.push({sources_[source_idx]->next(), source_idx});
    }
    
    return order;
}

size_t OrderReader::getTotalOrdersProcessed() const {
    size_t total = 0;
    for (const auto& source : sources_) {
        total += source->current_idx;
    }
    return total;
}

size_t OrderReader::getMemoryUsage() const {
    size_t total = sizeof(*this);
    for (const auto& source : sources_) {
        total += sizeof(*source);
        total += source->orders.size() * sizeof(OrderUpdate);
    }
    return total;
}