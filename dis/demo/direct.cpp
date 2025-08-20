#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <random>

int main() {
    // 设置参数
    const size_t num_sequences = 500;        // 500 路有序数据
    const size_t total_elements = 20000000;  // 总数据量为 20M

    // 每路数据的大小
    size_t sequence_size = total_elements / num_sequences;

    // 用于存储所有有序数据的容器
    std::vector<int> all_data;

    // 创建随机数生成器
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 1000000);  // 随机数范围

    // 构造 500 路有序数据
    for (size_t i = 0; i < num_sequences; ++i) {
        std::vector<int> sequence(sequence_size);

        // 生成有序数据（这里每个序列内部是有序的）
        sequence[0] = dis(gen);
        for (size_t j = 1; j < sequence_size; ++j) {
            sequence[j] = sequence[j-1] + dis(gen) % 10;  // 保证有序性
        }

        // 将有序数据添加到总容器中
        all_data.insert(all_data.end(), sequence.begin(), sequence.end());
    }

    // 打印数据大小
    std::cout << "Total data size: " << all_data.size() << " elements" << std::endl;

    // 记录排序开始时间
    auto start = std::chrono::high_resolution_clock::now();

    // 使用 std::sort 排序所有数据
    std::sort(all_data.begin(), all_data.end());

    // 记录排序结束时间
    auto end = std::chrono::high_resolution_clock::now();

    // 计算排序耗时
    std::chrono::duration<double> duration = end - start;
    std::cout << "Sorting time: " << duration.count() << " seconds" << std::endl;

    return 0;
}
