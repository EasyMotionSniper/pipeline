// micro_bucket_pq_bench.cpp
// C++20 single-file demo: bucket-style priority queues for microsecond timestamps (9:30-15:00)
// Implements: (A) baseline std::priority_queue, (B) RadixHeap64 (amortized O(1) for monotone pops)
// Build:   g++ -O3 -march=native -pthread -std=c++20 micro_bucket_pq_bench.cpp -o micro_bucket_pq_bench
// Run:     ./micro_bucket_pq_bench --items 20000000 --ways 500 --mode all
//          ./micro_bucket_pq_bench --items 4000000 --ways 500 --mode all   # quick sanity
// Notes:   This focuses on the PQ itself. If you want the full two-level parallel merge, see previous file.

#include <bits/stdc++.h>
using namespace std;

// --------------------------- Time helpers ---------------------------
static inline uint64_t now_ns(){using namespace std::chrono;return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();}
struct Timer{string name; uint64_t t0; bool stopped=false; double ms=0; explicit Timer(string n):name(move(n)),t0(now_ns()){} ~Timer(){if(!stopped) stop();} double stop(){if(stopped) return ms; stopped=true; ms=(now_ns()-t0)/1e6; return ms;}};

// 9:30:00 to 15:00:00 in seconds
constexpr uint64_t T9_30 = 9ull*3600ull + 30ull*60ull; // 34200
constexpr uint64_t T15_00 = 15ull*3600ull;             // 54000
constexpr uint64_t RANGE_SEC = T15_00 - T9_30;         // 19800
constexpr uint64_t US_PER_SEC = 1'000'000ull;

// --------------------------- Model ---------------------------
struct Update { uint64_t ts_us; uint32_t way; uint64_t id; };
static_assert(sizeof(Update)<=32);

// --------------------------- Synthetic generator ---------------------------
struct GenConfig { size_t total_items=20'000'000; size_t ways=500; uint64_t seed=42; double skew=0.2; uint64_t base_gap_us=150; };

vector<vector<Update>> make_streams(const GenConfig& cfg){
    vector<vector<Update>> streams(cfg.ways);
    size_t per_way = cfg.total_items / cfg.ways;
    size_t rem = cfg.total_items % cfg.ways;
    std::mt19937_64 rng(cfg.seed);
    std::normal_distribution<double> noise(0.0, cfg.skew);

    uint64_t id=0;
    for(size_t w=0; w<cfg.ways; ++w){
        size_t n = per_way + (w<rem?1:0);
        streams[w].reserve(n);
        uint64_t ts = (T9_30*US_PER_SEC) + (w%100); // small stagger per way
        double gap = (double)cfg.base_gap_us*(1.0 + 0.25*(double)w/(double)cfg.ways);
        for(size_t i=0;i<n;++i){
            if(cfg.skew>0) gap = max(1.0, gap + noise(rng));
            ts += (uint64_t)gap; // strictly increasing per way
            streams[w].push_back(Update{ts,(uint32_t)w,id++});
        }
    }
    return streams;
}

// --------------------------- Baseline PQ (std) ---------------------------
struct Node { uint64_t key; Update val; };
struct Greater { bool operator()(const Node&a, const Node&b) const { return a.key > b.key; } };

size_t merge_baseline(vector<vector<Update>>& streams, vector<Update>& out){
    out.clear(); out.reserve(accumulate(streams.begin(),streams.end(),0ull,[](uint64_t s,const auto&v){return s+v.size();}));
    vector<size_t> idx(streams.size(),0);
    priority_queue<Node, vector<Node>, Greater> pq;
    for(size_t w=0; w<streams.size(); ++w){ if(!streams[w].empty()) { pq.push(Node{streams[w][0].ts_us, streams[w][0]}); idx[w]=1; }}
    while(!pq.empty()){
        auto n = pq.top(); pq.pop(); out.push_back(n.val); size_t w=n.val.way; if(idx[w]<streams[w].size()){ auto &u=streams[w][idx[w]++]; pq.push(Node{u.ts_us,u}); }
    }
    return out.size();
}

// --------------------------- Radix Heap (64-bit, monotone keys) ---------------------------
// Based on Imai & Asai's radix heap; keys must be non-decreasing across pops.
// Complexity: push amortized O(1), pop amortized O(1), memory O(N + B), B≈65.
class RadixHeap64 {
public:
    using Key = uint64_t;
    struct Item { Key key; Update val; };

    RadixHeap64(): last_(0), size_(0) {}

    bool empty() const { return size_==0; }
    size_t size() const { return size_; }

    void push(Key key, const Update& v){
        // invariant: key >= last_
        size_++;
        size_t b = bucket_index(key);
        buckets_[b].push_back(Item{key,v});
    }

    Item pop(){
        if(buckets_[0].empty()) refill();
        auto it = buckets_[0].back(); buckets_[0].pop_back(); size_--; last_ = it.key; return it;
    }

private:
    // 65 buckets; bucket 0 contains items with key==last_; bucket i>0 contains keys with msb at i-1
    array<vector<Item>, 65> buckets_{};
    Key last_;
    size_t size_;

    static inline int clz64(uint64_t x){ return x? __builtin_clzll(x) : 64; }

    size_t bucket_index(Key key) const {
        uint64_t diff = key ^ last_;
        if (diff==0) return 0;
        // msb position from left -> bucket id
        int lz = clz64(diff); // 0..64
        int msb_index = 63 - lz; // 0..63
        return (size_t)msb_index + 1; // 1..64
    }

    void refill(){
        // find first non-empty bucket with i>0
        size_t i=1; while(i<buckets_.size() && buckets_[i].empty()) ++i;
        if(i==buckets_.size()) return; // empty overall
        // new last_ is min key in bucket i
        Key new_last = buckets_[i][0].key;
        for(auto &e: buckets_[i]) if(e.key < new_last) new_last = e.key;
        // redistribute bucket i into lower buckets according to new_last
        for(auto &e: buckets_[i]){
            uint64_t diff = e.key ^ new_last;
            size_t b = (diff==0)?0:(63 - clz64(diff)) + 1;
            buckets_[b].push_back(std::move(e));
        }
        buckets_[i].clear();
        last_ = new_last;
    }
};

size_t merge_radixheap(vector<vector<Update>>& streams, vector<Update>& out){
    out.clear(); out.reserve(accumulate(streams.begin(),streams.end(),0ull,[](uint64_t s,const auto&v){return s+v.size();}));
    vector<size_t> idx(streams.size(),0);
    RadixHeap64 pq;
    for(size_t w=0; w<streams.size(); ++w){ if(!streams[w].empty()){ pq.push(streams[w][0].ts_us, streams[w][0]); idx[w]=1; }}
    while(!pq.empty()){
        auto it = pq.pop(); out.push_back(it.val); size_t w=it.val.way; if(idx[w]<streams[w].size()){ auto &u=streams[w][idx[w]++]; pq.push(u.ts_us,u);} }
    return out.size();
}

// --------------------------- Verify & Bench ---------------------------
bool check_sorted(const vector<Update>& out){ for(size_t i=1;i<out.size();++i) if(out[i-1].ts_us>out[i].ts_us) return false; return true; }

struct Args{ size_t items=4'000'000; size_t ways=500; string mode="all"; double skew=0.2; uint64_t base_gap_us=150; };
Args parse(int argc,char**argv){ Args a; for(int i=1;i<argc;++i){ string s=argv[i]; auto next=[&]{return (i+1<argc)?string(argv[++i]):string();};
    if(s=="--items") a.items=stoull(next());
    else if(s=="--ways") a.ways=stoull(next());
    else if(s=="--mode") a.mode=next();
    else if(s=="--skew") a.skew=stod(next());
    else if(s=="--gap") a.base_gap_us=stoull(next());
} return a; }

int main(int argc,char**argv){ ios::sync_with_stdio(false); cin.tie(nullptr);
    auto args = parse(argc,argv);
    GenConfig cfg; cfg.total_items=args.items; cfg.ways=args.ways; cfg.skew=args.skew; cfg.base_gap_us=args.base_gap_us;
    cerr << "Generating streams ("<<args.ways<<" ways, "<<args.items<<" items, microsecond ts) ...\n";
    auto streams = make_streams(cfg);
    vector<Update> out;
    auto bench=[&](string name, auto fn){ Timer t(name); size_t n=fn(); double ms=t.stop(); bool ok=check_sorted(out)&&n==args.items; double mops=n/(ms/1000.0)/1e6; cout<<left<<setw(12)<<name<<" | items="<<n<<", time_ms="<<fixed<<setprecision(2)<<ms<<", thrpt="<<setprecision(3)<<mops<<" Mops/s, sorted="<<(ok?"Y":"N")<<"\n"; };

    if(args.mode=="baseline"||args.mode=="all") bench("std_heap", [&]{ return merge_baseline(streams,out); });
    if(args.mode=="radix"||args.mode=="all") bench("radixheap", [&]{ return merge_radixheap(streams,out); });
    return 0;
}
