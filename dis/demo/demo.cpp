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
    std::string data = "mmm";
    auto buffer = arrow::Buffer::FromString(data);
    auto input = std::make_shared<arrow::io::BufferReader>(buffer);
    
    // Use the correct API
    auto result = parquet::arrow::OpenFile(input, arrow::default_memory_pool());
    if (!result.ok()) {
        return -1;
    }
    return 0;
}
    