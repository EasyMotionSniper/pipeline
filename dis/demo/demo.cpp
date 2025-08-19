#include <iostream>
#include <fstream>
#include <vector>
#include <random>
#include <memory>
#include <string>
#include <filesystem>

// Arrow和Parquet库
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/writer.h>
#include <parquet/arrow/reader.h>
#include <arrow/io/interfaces.h>



int main() {
    std::string rawData = "1,2,3,4,5,6,7,8,9,10";
    std::shared_ptr<parquet::arrow::FileReader> reader_;
    auto buffer = std::make_shared<arrow::Buffer>(
            reinterpret_cast<const uint8_t*>(rawData.data()), rawData.size());
        auto input = std::make_shared<arrow::io::BufferReader>(buffer);

        arrow::Result<std::unique_ptr<parquet::arrow::FileReader>> res =
            parquet::arrow::OpenFile(input, arrow::default_memory_pool());
        if (!res.ok()) throw std::runtime_error(res.status().ToString());
        reader_ = std::move(res.ValueUnsafe());

        int total_row_groups_ = reader_->num_row_groups();

        

    return 0;
}
    