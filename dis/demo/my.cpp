#include <tlx/container/loser_tree.hpp>
#include <iostream>
#include <vector>
#include <tlx/thread_pool.hpp>


#include <iostream>
#include <vector>
#include <thread>
#include <functional>
#include <atomic>

tlx::ThreadPool pool;
// 目标类
class MyClass {
public:
    MyClass(size_t rows, size_t cols, size_t numThreads)
        : rows(rows), cols(cols), array(rows, std::vector<int>(cols, 0))
           {}

    void fillArray() {
        // 使用线程池并行填充二维数组
        std::atomic<size_t> counter(0);

        for (size_t i = 0; i < rows; ++i) {
            for (size_t j = 0; j < cols; ++j) {

                pool.enqueue([this, i, j, &counter] {
                    // 这里可以根据任务的复杂度来填充数据
                    array[i][j] = i * cols + j;  // 示例填充方式：根据索引填充
                    counter++;
                    if (counter == rows * cols) {
                        std::cout << "Array filling completed!" << std::endl;
                    }
                });
            }
        }
    }

    void printArray() const {
        for (const auto& row : array) {
            for (const auto& elem : row) {
                std::cout << elem << " ";
            }
            std::cout << std::endl;
        }
    }

private:
    size_t rows, cols;
    std::vector<std::vector<int>> array;
};



int main() {
    std::vector<std::vector<int>> vecs = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};

    tlx::LoserTree<false, int, std::less<>> lt(vecs.size());
    for (size_t i = 0; i < vecs.size(); ++i)
    {
        lt.insert_start(&vecs[i][0], i, false);
    }
    std::vector<int> index(vecs.size());
    lt.init();
    size_t cnt = 0;
    while (cnt < 9)
    {
        int idx = lt.min_source();
        std::cout << idx << " " << vecs[idx][index[idx]] << std::endl;
        ++index[idx];
        if (index[idx] >= vecs[idx].size())
        {
            lt.delete_min_insert(nullptr, true);
        }
        else
        {
            lt.delete_min_insert(&vecs[idx][index[idx]], false);
        }
        ++cnt;
    }

    
     // 创建一个 MyClass 对象，包含 4 行 5 列的二维数组，并使用 4 个线程
    MyClass myObject(4, 5, 4);

    // 使用线程池并行填充数组
    myObject.fillArray();

    // 等待填充完成（根据实际情况添加同步机制，比如等待所有线程完成）
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 打印结果
    myObject.printArray();
    
}