// pq_bench.cpp
#include <bits/stdc++.h>
using namespace std;

// -------------------- Data Model --------------------
struct OrderUpdate {
    int64_t ts;
    uint64_t id;
    uint32_t way;
};

// -------------------- Priority Queue Implementations --------------------

// 1. std::priority_queue wrapper
struct HeapPQ {
    struct Cmp {
        bool operator()(const OrderUpdate& a, const OrderUpdate& b) const {
            return a.ts > b.ts; // min-heap
        }
    };
    std::priority_queue<OrderUpdate, vector<OrderUpdate>, Cmp> pq;

    void push(const OrderUpdate& u) { pq.push(u); }
    bool pop(OrderUpdate& out) {
        if (pq.empty()) return false;
        out = pq.top();
        pq.pop();
        return true;
    }
};

// 2. BucketPQ (parametric granularity)
struct BucketPQ {
    int64_t start_ts, end_ts, bucket_size;
    size_t n_buckets;
    vector<vector<OrderUpdate>> buckets;
    size_t cur_idx = 0;

    BucketPQ(int64_t start_ts_, int64_t end_ts_, int64_t bucket_size_)
        : start_ts(start_ts_), end_ts(end_ts_), bucket_size(bucket_size_) {
        int64_t span = end_ts - start_ts;
        n_buckets = (span + bucket_size - 1) / bucket_size;
        buckets.resize(n_buckets);
    }

    inline size_t index_for(int64_t ts) const {
        return (ts - start_ts) / bucket_size;
    }

    void push(const OrderUpdate& upd) {
        size_t idx = index_for(upd.ts);
        buckets[idx].push_back(upd);
    }

    bool pop(OrderUpdate& out) {
        while (cur_idx < n_buckets) {
            auto& b = buckets[cur_idx];
            if (!b.empty()) {
                // 为了稳定顺序，按 timestamp 排一次
                if (b.size() > 1 && !std::is_sorted(b.begin(), b.end(), 
                        [](auto& a, auto& b){ return a.ts < b.ts; })) {
                    std::sort(b.begin(), b.end(), [](auto& a, auto& b){ return a.ts < b.ts; });
                }
                out = b.back();
                b.pop_back();
                return true;
            }
            ++cur_idx;
        }
        return false;
    }
};

// 3. RadixHeap64
template <typename T>
struct RadixHeap64 {
    using Key = uint64_t;
    static constexpr int B = 64;
    vector<vector<pair<Key,T>>> buckets;
    Key last;
    size_t sz;

    RadixHeap64() : buckets(B+1), last(0), sz(0) {}

    bool empty() const { return sz == 0; }
    size_t size() const { return sz; }

    inline int bucket_index(Key x) const {
        if (x == last) return 0;
        return 64 - __builtin_clzll(x ^ last);
    }

    void push(Key key, const T& val) {
        int b = bucket_index(key);
        buckets[b].emplace_back(key,val);
        sz++;
    }

    pair<Key,T> pop() {
        if (buckets[0].empty()) {
            int i = 1;
            while (i <= B && buckets[i].empty()) i++;
            assert(i <= B);

            Key new_last = buckets[i][0].first;
            for (auto& kv : buckets[i]) {
                if (kv.first < new_last) new_last = kv.first;
            }
            last = new_last;

            for (auto& kv : buckets[i]) {
                int b = bucket_index(kv.first);
                buckets[b].emplace_back(kv);
            }
            buckets[i].clear();
        }
        auto kv = buckets[0].back();
        buckets[0].pop_back();
        sz--;
        return kv;
    }
};

// -------------------- Benchmark Harness --------------------
static inline int64_t hms_to_us(int h, int m, int s, int us=0) {
    return ((int64_t)h*3600 + m*60 + s) * 1'000'000 + us;
}

template<typename PQ, typename... Args>
void run_bench(const string& name, int N, int streams, Args&&... args) {
    int64_t start = hms_to_us(9,30,0);
    int64_t end   = hms_to_us(15,0,0);
    uint64_t id=0;
    std::mt19937_64 rng(42);
    std::uniform_int_distribution<int64_t> dist(start, end);

    PQ pq(std::forward<Args>(args)...);

    // push
    auto t1 = chrono::high_resolution_clock::now();
    for(int w=0; w<streams; ++w){
        for(int i=0;i<N;++i){
            int64_t ts = dist(rng);
            if constexpr (std::is_same_v<PQ, RadixHeap64<OrderUpdate>>) {
                pq.push((uint64_t)ts, OrderUpdate{ts,id++, (uint32_t)w});
            } else {
                pq.push(OrderUpdate{ts,id++, (uint32_t)w});
            }
        }
    }
    auto t2 = chrono::high_resolution_clock::now();

    // pop
    size_t cnt=0;
    int64_t last_ts=-1;
    if constexpr (std::is_same_v<PQ, RadixHeap64<OrderUpdate>>) {
        while(!pq.empty()) {
            auto kv = pq.pop();
            auto u = kv.second;
            if (u.ts < last_ts) { cerr<<"Error!\n"; break; }
            last_ts = u.ts;
            cnt++;
        }
    } else {
        OrderUpdate u;
        while(pq.pop(u)) {
            if (u.ts < last_ts) { cerr<<"Error!\n"; break; }
            last_ts = u.ts;
            cnt++;
        }
    }
    auto t3 = chrono::high_resolution_clock::now();

    double push_ms = chrono::duration<double, milli>(t2-t1).count();
    double pop_ms  = chrono::duration<double, milli>(t3-t2).count();
    double total   = chrono::duration<double, milli>(t3-t1).count();
    cout << name << " : " << cnt << " events | push " << push_ms << " ms | pop " 
         << pop_ms << " ms | total " << total << " ms\n";
}

// -------------------- main --------------------
int main() {
    int N = 100000;   // 每路事件数
    int streams = 500; // 路数
    cout << "Benchmark with "<<streams<<" streams × "<<N<<" events\n";

    run_bench<HeapPQ>("std::priority_queue", N, streams);
    run_bench<BucketPQ>("BucketPQ(10us)", N, streams, hms_to_us(9,30,0), hms_to_us(15,0,0), 10);
    run_bench<BucketPQ>("BucketPQ(100us)", N, streams, hms_to_us(9,30,0), hms_to_us(15,0,0), 100);
    run_bench<BucketPQ>("BucketPQ(1ms)", N, streams, hms_to_us(9,30,0), hms_to_us(15,0,0), 1000);
    run_bench<RadixHeap64<OrderUpdate>>("RadixHeap64", N, streams);

    return 0;
}
