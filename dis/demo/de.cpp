#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>
#include <queue>
#include <tlx/container/loser_tree.hpp>
#include <tlx/die.hpp>
#include <tlx/logger.hpp>

struct MyInt {
    size_t value_ = 1;
    MyInt() = default;
    explicit MyInt(size_t value) : value_(value) {}

    bool operator==(const MyInt& b) const {
        return value_ == b.value_;
    }
};

struct MyIntCompare {
    bool operator()(const MyInt& a, const MyInt& b) const {
        return a.value_ > b.value_;  // Min-heap
    }
};

// 计算时间差
double time_delta(const std::chrono::steady_clock::time_point& a,
                  const std::chrono::steady_clock::time_point& b)
{
    return std::chrono::duration_cast<std::chrono::microseconds>(b - a)
               .count() /
           1e6;
}

// 败者树 Copy benchmark
void benchmark_losertree_copy(const char* benchmark, size_t num_vectors, size_t vector_size)
{
    using Vector = std::vector<MyInt>;
    using steady_clock = std::chrono::steady_clock;

    std::vector<Vector> vecs;
    size_t total_size = 0;

    std::minstd_rand rng(123456);

    steady_clock::time_point tp1 = steady_clock::now();

    for (size_t i = 0; i < num_vectors; ++i)
    {
        std::vector<MyInt> vec1;
        vec1.reserve(vector_size);

        for (size_t j = 0; j < vector_size; ++j)
            vec1.emplace_back(MyInt(rng()));

        std::sort(vec1.begin(), vec1.end(), MyIntCompare());
        total_size += vec1.size();
        vecs.emplace_back(std::move(vec1));
    }

    steady_clock::time_point tp2 = steady_clock::now();

    tlx::LoserTreeCopy<false, MyInt, MyIntCompare> lt(vecs.size());

    std::vector<Vector::const_iterator> lt_iter(vecs.size());
    size_t remaining_inputs = 0;

    for (size_t i = 0; i < vecs.size(); ++i)
    {
        lt_iter[i] = vecs[i].begin();

        if (lt_iter[i] == vecs[i].end())
        {
            lt.insert_start(nullptr, i, true);
        }
        else
        {
            lt.insert_start(&*lt_iter[i], i, false);
            ++remaining_inputs;
        }
    }

    lt.init();

    std::vector<MyInt> result;
    result.reserve(total_size);

    while (remaining_inputs != 0)
    {
        unsigned top = lt.min_source();
        result.emplace_back(*lt_iter[top]++);

        if (lt_iter[top] != vecs[top].end())
        {
            lt.delete_min_insert(&*lt_iter[top], false);
        }
        else
        {
            lt.delete_min_insert(nullptr, true);
            --remaining_inputs;
        }
    }

    steady_clock::time_point tp3 = steady_clock::now();

    die_unequal(result.size(), total_size);

    std::cout << "RESULT"
              << " benchmark=" << benchmark << " num_vectors=" << num_vectors
              << " vector_size=" << vector_size
              << " init_time=" << time_delta(tp1, tp2)
              << " merge_time=" << time_delta(tp2, tp3) << '\n';
}

// 败者树 Pointer benchmark
void benchmark_losertree_pointer(const char* benchmark, size_t num_vectors, size_t vector_size)
{
    using Vector = std::vector<MyInt>;
    using steady_clock = std::chrono::steady_clock;

    std::vector<Vector> vecs;
    size_t total_size = 0;

    std::minstd_rand rng(123456);

    steady_clock::time_point tp1 = steady_clock::now();

    for (size_t i = 0; i < num_vectors; ++i)
    {
        std::vector<MyInt> vec1;
        vec1.reserve(vector_size);

        for (size_t j = 0; j < vector_size; ++j)
            vec1.emplace_back(MyInt(rng()));

        std::sort(vec1.begin(), vec1.end(), MyIntCompare());
        total_size += vec1.size();
        vecs.emplace_back(std::move(vec1));
    }

    steady_clock::time_point tp2 = steady_clock::now();

    tlx::LoserTreePointer<false, MyInt, MyIntCompare> lt(vecs.size());

    std::vector<Vector::const_iterator> lt_iter(vecs.size());
    size_t remaining_inputs = 0;

    for (size_t i = 0; i < vecs.size(); ++i)
    {
        lt_iter[i] = vecs[i].begin();

        if (lt_iter[i] == vecs[i].end())
        {
            lt.insert_start(nullptr, i, true);
        }
        else
        {
            lt.insert_start(&*lt_iter[i], i, false);
            ++remaining_inputs;
        }
    }

    lt.init();

    std::vector<MyInt> result;
    result.reserve(total_size);

    while (remaining_inputs != 0)
    {
        unsigned top = lt.min_source();
        result.emplace_back(*lt_iter[top]++);

        if (lt_iter[top] != vecs[top].end())
        {
            lt.delete_min_insert(&*lt_iter[top], false);
        }
        else
        {
            lt.delete_min_insert(nullptr, true);
            --remaining_inputs;
        }
    }

    steady_clock::time_point tp3 = steady_clock::now();

    die_unequal(result.size(), total_size);

    std::cout << "RESULT"
              << " benchmark=" << benchmark << " num_vectors=" << num_vectors
              << " vector_size=" << vector_size
              << " init_time=" << time_delta(tp1, tp2)
              << " merge_time=" << time_delta(tp2, tp3) << '\n';
}

// 优先队列 benchmark
void benchmark_priority_queue(const char* benchmark, size_t num_vectors, size_t vector_size)
{
    using Vector = std::vector<int>;
    using steady_clock = std::chrono::steady_clock;

    std::vector<Vector> vecs;
    size_t total_size = 0;

    std::minstd_rand rng(123456);

    steady_clock::time_point tp1 = steady_clock::now();

    for (size_t i = 0; i < num_vectors; ++i)
    {
        std::vector<int> vec1;
        vec1.reserve(vector_size);

        for (size_t j = 0; j < vector_size; ++j)
            vec1.emplace_back(int(rng()));

        std::sort(vec1.begin(), vec1.end());
        total_size += vec1.size();
        vecs.emplace_back(std::move(vec1));
    }

    steady_clock::time_point tp2 = steady_clock::now();

    // 创建优先队列
    std::priority_queue<std::pair<int, int>, std::vector<std::pair<int, int>>> pq;
    std::vector<int> lt_iter(vecs.size());
    for (size_t i = 0; i < vecs.size(); ++i)
    {
        pq.push({vecs[i][0], i});
        ++lt_iter[i];
    }

    std::vector<MyInt> result;
    result.reserve(total_size);
    size_t cnt = 0;
    while (!pq.empty())
    {
        ++cnt;
        auto [val, idx] = pq.top();
        result.emplace_back(val);
        if (lt_iter[idx] < vecs[idx].size())
        {
            pq.push({vecs[idx][lt_iter[idx]], idx});
            ++lt_iter[idx];
        }
        pq.pop();
    }

    steady_clock::time_point tp3 = steady_clock::now();

    die_unequal(result.size(), total_size);
    std::cout << cnt << std::endl;
    std::cout << "RESULT"
              << " benchmark=" << benchmark << " num_vectors=" << num_vectors
              << " vector_size=" << vector_size
              << " init_time=" << time_delta(tp1, tp2)
              << " merge_time=" << time_delta(tp2, tp3) << '\n';
}

int main(int argc, char* argv[])
{
    size_t num_vectors = 500;  // 500路
    size_t vector_size = 20000000 / num_vectors;  // 总共20M条数据

    // benchmark LoserTreeCopy, LoserTreePointer 和 PriorityQueue
    std::cout << "Benchmarking LoserTreeCopy..." << std::endl;
    benchmark_losertree_copy("LoserTreeCopy", num_vectors, vector_size);

    std::cout << "Benchmarking LoserTreePointer..." << std::endl;
    benchmark_losertree_pointer("LoserTreePointer", num_vectors, vector_size);

    std::cout << "Benchmarking PriorityQueue..." << std::endl;
    benchmark_priority_queue("PriorityQueue", num_vectors, vector_size);

    return 0;
}
