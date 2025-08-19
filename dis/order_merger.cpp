// kway_merge_bench.cpp
// C++20 single-file demo: multi-way ordered stream merge (500 ways, 20M items)
// Implements: (A) baseline single-heap, (B) batched single-thread, (C) two-level parallel merge with SPSC ring queues.
// Build:   g++ -O3 -march=native -pthread -std=c++20 kway_merge_bench.cpp -o kway_merge_bench
// Run:     ./kway_merge_bench --items 20000000 --ways 500 --groups 10 --batch 64 --mode all
// Modes:   baseline | batched | parallel | all
// Notes:   Synthetic data generator produces per-way increasing timestamps; options let you tweak skew.
//          The code verifies global sortedness and reports throughput.

#include <bits/stdc++.h>
using namespace std;

// --------------------------- Utilities ---------------------------
static inline uint64_t now_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

struct Timer {
    string name; uint64_t t0; bool stopped=false; double ms=0.0;
    explicit Timer(string n): name(std::move(n)), t0(now_ns()) {}
    ~Timer(){ if(!stopped) stop(); }
    double stop(){ if(stopped) return ms; stopped=true; ms=(now_ns()-t0)/1e6; return ms; }
};

// --------------------------- Data Model ---------------------------
struct OrderUpdate {
    int64_t ts;             // timestamp (ns or us). Monotonic per stream.
    uint64_t id;            // synthetic id
    uint32_t way;           // originating stream id
    // payload could go here (kept tiny to maximize memory BW)
};

// Pack to 24 bytes on most platforms; keep tight for cache
static_assert(sizeof(OrderUpdate) <= 32);

// --------------------------- Synthetic Generator ---------------------------
struct GenConfig {
    size_t total_items = 20'000'000;
    size_t ways = 500;
    double skew = 0.0;   // per-way drift; 0 = uniform pace, >0 introduces random walk drift
    uint64_t base_gap = 100; // nominal timestamp gap between consecutive items per way
    uint64_t start_ts = 0;
    uint64_t seed = 42;
};

vector<vector<OrderUpdate>> make_streams(const GenConfig& cfg) {
    vector<vector<OrderUpdate>> streams(cfg.ways);
    size_t per_way = cfg.total_items / cfg.ways;
    size_t remainder = cfg.total_items % cfg.ways;

    std::mt19937_64 rng(cfg.seed);
    std::normal_distribution<double> drift(0.0, cfg.skew);

    uint64_t global_id = 0;
    for (size_t w=0; w<cfg.ways; ++w) {
        size_t n = per_way + (w < remainder ? 1 : 0);
        streams[w].reserve(n);
        int64_t ts = (int64_t)cfg.start_ts + int64_t(w); // stagger starts slightly
        double local_gap = double(cfg.base_gap) * (1.0 + 0.01*(int)w/(int)cfg.ways);
        for (size_t i=0; i<n; ++i) {
            // drift on gap to create overlaps across ways
            if (cfg.skew > 0.0) local_gap = max(1.0, local_gap + drift(rng));
            ts += (int64_t)(local_gap);
            streams[w].push_back(OrderUpdate{ts, global_id++, (uint32_t)w});
        }
    }
    return streams;
}

// --------------------------- Per-way Cursor with Batch Cache ---------------------------
struct StreamCursor {
    const OrderUpdate* data = nullptr;
    size_t size = 0;
    size_t idx = 0;

    // small cache to reduce heap ops: read-ahead window
    static constexpr size_t MAX_BATCH = 1024;
    vector<OrderUpdate> cache; // next batch sorted within this stream inherently
    size_t cpos=0;

    StreamCursor() = default;
    StreamCursor(const vector<OrderUpdate>& vec, size_t batch): data(vec.data()), size(vec.size()) {
        cache.reserve(min(batch, MAX_BATCH));
    }

    bool refill(size_t batch) {
        cache.clear(); cpos=0;
        size_t remain = size - idx;
        size_t take = min(remain, batch);
        if (take==0) return false;
        // copy contiguous range (already sorted)
        const OrderUpdate* src = data + idx;
        cache.insert(cache.end(), src, src+take);
        idx += take;
        return true;
    }

    inline const OrderUpdate* peek() const {
        if (cpos < cache.size()) return &cache[cpos];
        return nullptr;
    }
    inline const OrderUpdate* pop() {
        if (cpos < cache.size()) return &cache[cpos++];
        return nullptr;
    }
};

// --------------------------- SPSC Ring (bounded) ---------------------------
// lock-free single-producer single-consumer ring buffer for OrderUpdate
class SPSC {
public:
    explicit SPSC(size_t cap_pow2) {
        // capacity must be power of two
        size_t cap=1; while (cap < cap_pow2) cap<<=1; capacity_ = cap;
        buf_.resize(capacity_);
    }
    bool push(const OrderUpdate& v) {
        size_t h = head_.load(std::memory_order_relaxed);
        size_t n = (h+1) & (capacity_-1);
        if (n == tail_.load(std::memory_order_acquire)) return false; // full
        buf_[h] = v;
        head_.store(n, std::memory_order_release);
        return true;
    }
    bool pop(OrderUpdate& out) {
        size_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) return false; // empty
        out = buf_[t];
        tail_.store((t+1)&(capacity_-1), std::memory_order_release);
        return true;
    }
    bool empty() const {
        return tail_.load(std::memory_order_acquire) == head_.load(std::memory_order_acquire);
    }
private:
    vector<OrderUpdate> buf_;
    size_t capacity_{};
    std::atomic<size_t> head_{0}, tail_{0};
};

// --------------------------- Baseline Single-Heap Merge ---------------------------
struct HeapNode { OrderUpdate val; size_t way; };
struct CmpNode { bool operator()(const HeapNode& a, const HeapNode& b) const { return a.val.ts > b.val.ts; } };

size_t merge_baseline(vector<vector<OrderUpdate>>& streams, vector<OrderUpdate>& out) {
    out.clear(); out.reserve(size_t( accumulate(streams.begin(), streams.end(), 0ull, [](uint64_t s, const auto& v){return s+v.size();}) ));
    using PQ = std::priority_queue<HeapNode, vector<HeapNode>, CmpNode>;
    PQ pq; pq = PQ();
    vector<size_t> idx(streams.size(), 0);

    for (size_t w=0; w<streams.size(); ++w) if (!streams[w].empty()) {
        pq.push(HeapNode{streams[w][0], w});
        idx[w] = 1;
    }

    while (!pq.empty()) {
        auto top = pq.top(); pq.pop();
        out.push_back(top.val);
        size_t w = top.way;
        if (idx[w] < streams[w].size()) {
            pq.push(HeapNode{streams[w][idx[w]++], w});
        }
    }
    return out.size();
}

// --------------------------- Batched Single-Thread Merge ---------------------------
size_t merge_batched(vector<vector<OrderUpdate>>& streams, vector<OrderUpdate>& out, size_t batch) {
    out.clear(); out.reserve(size_t( accumulate(streams.begin(), streams.end(), 0ull, [](uint64_t s, const auto& v){return s+v.size();}) ));
    vector<StreamCursor> cursors; cursors.reserve(streams.size());
    for (auto& s: streams) cursors.emplace_back(StreamCursor{s, batch});

    using PQ = std::priority_queue<HeapNode, vector<HeapNode>, CmpNode>;
    PQ pq;
    for (size_t w=0; w<cursors.size(); ++w) if (cursors[w].refill(batch)) {
        pq.push(HeapNode{*cursors[w].peek(), w});
        cursors[w].pop();
    }
    while (!pq.empty()) {
        auto top = pq.top(); pq.pop();
        out.push_back(top.val);
        auto& cur = cursors[top.way];
        if (const OrderUpdate* nxt = cur.peek()) {
            pq.push(HeapNode{*nxt, top.way});
            cur.pop();
        } else if (cur.refill(batch)) {
            pq.push(HeapNode{*cur.peek(), top.way});
            cur.pop();
        }
    }
    return out.size();
}

// --------------------------- Parallel Two-Level Merge ---------------------------
struct GroupWorker {
    size_t gid;
    vector<size_t> ways;
    vector<StreamCursor> cursors;
    size_t batch;
    SPSC* queue; // producer
    thread th;

    GroupWorker(size_t gid_, vector<size_t> ways_, vector<vector<OrderUpdate>>& streams, size_t batch_, SPSC* q)
        : gid(gid_), ways(std::move(ways_)), batch(batch_), queue(q) {
        cursors.reserve(ways.size());
        for (auto w: ways) cursors.emplace_back(StreamCursor{streams[w], batch});
    }

    void run() {
        using PQ = std::priority_queue<HeapNode, vector<HeapNode>, CmpNode>;
        PQ pq;
        for (size_t i=0; i<cursors.size(); ++i) if (cursors[i].refill(batch)) {
            pq.push(HeapNode{*cursors[i].peek(), i});
            cursors[i].pop();
        }
        OrderUpdate tmp;
        while (!pq.empty()) {
            auto top = pq.top(); pq.pop();
            // push to queue; spin if full (bounded backpressure)
            while (!queue->push(top.val)) {
                std::this_thread::yield();
            }
            auto& cur = cursors[top.way];
            if (const OrderUpdate* nxt = cur.peek()) {
                pq.push(HeapNode{*nxt, top.way});
                cur.pop();
            } else if (cur.refill(batch)) {
                pq.push(HeapNode{*cur.peek(), top.way});
                cur.pop();
            }
        }
    }
};

size_t merge_parallel(vector<vector<OrderUpdate>>& streams, vector<OrderUpdate>& out, size_t groups, size_t batch, size_t qcap_pow2) {
    const size_t W = streams.size();
    groups = max<size_t>(1, min(groups, W));

    // Partition ways
    vector<vector<size_t>> parts(groups);
    for (size_t w=0; w<W; ++w) parts[w % groups].push_back(w);

    // Queues: one SPSC per group -> global consumer
    vector<unique_ptr<SPSC>> queues; queues.reserve(groups);
    for (size_t g=0; g<groups; ++g) queues.emplace_back(make_unique<SPSC>(qcap_pow2));

    // Start workers
    vector<unique_ptr<GroupWorker>> workers; workers.reserve(groups);
    for (size_t g=0; g<groups; ++g) {
        workers.emplace_back(make_unique<GroupWorker>(g, parts[g], streams, batch, queues[g].get()));
        workers.back()->th = thread([w=workers.back().get()]{ w->run(); });
    }

    // Global consumer: merge across group queues using a small heap of size=groups
    out.clear(); out.reserve(size_t( accumulate(streams.begin(), streams.end(), 0ull, [](uint64_t s, const auto& v){return s+v.size();}) ));

    struct QNode { OrderUpdate val; size_t gid; };
    struct CmpQ { bool operator()(const QNode& a, const QNode& b) const { return a.val.ts > b.val.ts; } };
    using PQ = priority_queue<QNode, vector<QNode>, CmpQ>;

    auto try_pop = [&](size_t g, OrderUpdate& v){ return queues[g]->pop(v); };

    // Prime heap with one from each queue when available
    PQ pq;
    vector<bool> finished(groups, false);
    size_t finished_cnt=0;

    // To detect worker completion: we join workers when their queues become empty and they have exited.
    // Simple approach: wait for threads after draining. We'll keep polling queues generously.

    // Preload
    for (size_t g=0; g<groups; ++g) {
        OrderUpdate v;
        if (try_pop(g, v)) pq.push(QNode{v, g});
    }

    // Drain loop
    while (true) {
        if (!pq.empty()) {
            auto top = pq.top(); pq.pop();
            out.push_back(top.val);
            OrderUpdate v;
            if (try_pop(top.gid, v)) {
                pq.push(QNode{v, top.gid});
            }
        } else {
            // heap empty: check for any new arrivals or worker completion
            bool any=false;
            for (size_t g=0; g<groups; ++g) {
                OrderUpdate v;
                if (try_pop(g, v)) { pq.push(QNode{v, g}); any=true; }
            }
            if (!any) {
                // check threads: if joinable and queue empty, join
                finished_cnt = 0;
                for (size_t g=0; g<groups; ++g) {
                    if (!finished[g]) {
                        // Peek if thread finished without blocking (try_join not in std).
                        // Use joinable + try to join when queue empty and no progress for a few spins.
                    }
                }
                // crude condition: if all workers done and all queues empty -> break
                bool all_joined=true;
                for (size_t g=0; g<groups; ++g) if (workers[g]->th.joinable()) { all_joined=false; break; }
                if (all_joined) break;
                // attempt to join any workers whose queues are empty and heap has nothing from them
                for (size_t g=0; g<groups; ++g) {
                    // best-effort join when queues appear idle
                    if (workers[g]->th.joinable()) {
                        // Busy-wait a moment, then join if queue quiet
                        if (queues[g]->empty()) {
                            workers[g]->th.join();
                        }
                    }
                }
                if (pq.empty()) {
                    // after joins and no new data -> done
                    bool any_joinable=false; for (auto& w: workers) if (w->th.joinable()) any_joinable=true;
                    if (!any_joinable) break;
                }
            }
        }
    }

    // Ensure all threads joined
    for (auto& w: workers) if (w->th.joinable()) w->th.join();

    return out.size();
}

// --------------------------- Verification ---------------------------
bool check_sorted(const vector<OrderUpdate>& out) {
    for (size_t i=1;i<out.size();++i) if (out[i-1].ts > out[i].ts) return false;
    return true;
}

// --------------------------- CLI & Bench ---------------------------
struct Args {
    size_t items=20'000'000;
    size_t ways=500;
    size_t groups=10;
    size_t batch=64;
    size_t qcap=1<<16; // queue capacity (power-of-two)
    double skew=0.2;
    string mode="all"; // baseline | batched | parallel | all
};

Args parse(int argc, char** argv){
    Args a; for (int i=1;i<argc;++i){ string s=argv[i]; auto next=[&]{return (i+1<argc)?string(argv[++i]):string();};
        if(s=="--items") a.items=stoull(next());
        else if(s=="--ways") a.ways=stoull(next());
        else if(s=="--groups") a.groups=stoull(next());
        else if(s=="--batch") a.batch=stoull(next());
        else if(s=="--qcap") a.qcap=stoull(next());
        else if(s=="--skew") a.skew=stod(next());
        else if(s=="--mode") a.mode=next();
    }
    return a;
}

int main(int argc, char** argv){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    Args args = parse(argc, argv);

    cerr << "Generating streams...\n";
    GenConfig cfg; cfg.total_items=args.items; cfg.ways=args.ways; cfg.skew=args.skew; cfg.base_gap=100; cfg.start_ts=0; cfg.seed=42;
    auto streams = make_streams(cfg);
    cerr << "Generated ways="<<streams.size()<<" total_items="<<args.items<<"\n";

    vector<OrderUpdate> out;

    auto bench = [&](string name, auto fn){
        Timer t(name);
        size_t n = fn();
        double ms = t.stop();
        bool ok = check_sorted(out) && (n==args.items);
        double mops = n / (ms/1000.0) / 1e6;
        cout << left << setw(12) << name << " | items="<< n << ", time_ms=" << fixed << setprecision(2) << ms
             << ", throughput=" << setprecision(3) << mops << " Mops/s" << ", sorted=" << (ok?"Y":"N") << "\n";
    };

    if (args.mode=="baseline" || args.mode=="all") {
        bench("baseline", [&]{ return merge_baseline(streams, out); });
    }
    if (args.mode=="batched" || args.mode=="all") {
        bench("batched", [&]{ return merge_batched(streams, out, args.batch); });
    }
    if (args.mode=="parallel" || args.mode=="all") {
        bench("parallel", [&]{ return merge_parallel(streams, out, args.groups, args.batch, args.qcap); });
    }

    return 0;
}
