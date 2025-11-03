#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <queue>
#include <chrono>

#include "Topology.h"
#include "TopologyDB.hpp"
#include "TopoLineCompact.hpp"
#include "Theory.h"
#include <unordered_set>
#include <unordered_map>
#include <sstream>

// ========== Configuration ==========
static constexpr size_t BUFFER_SIZE = 64u << 20;      // 64MB per buffer
static constexpr size_t BATCH_SIZE = 10000;           // Topologies per batch
static constexpr int MAX_DECO_PER_NODE = 3;

struct TargetSpec {
    bool all_nodes = true;
    std::unordered_set<int> nodes;
    bool do_S = true, do_I = true;
    enum class PrefixMode {None, Kind, HeadKind} prefix = PrefixMode::None;
};

enum class InFmt {Auto, DB, Line};

// ========== Gluing rules (original) ==========
static inline const std::vector<int>& allowed_S_params(int gval){
    static std::vector<int> dummy;
    static const std::vector<int> S_g4 = {1,882,883,22,32,23,33,42,991,9920,9902,92,93,97,98,912,915,916,917,331,43,53,99910,9913,924,925,927,928,929,934,936,937,938,940,941,942,943,956};
    static const std::vector<int> S_g6 = {1,882,883,884,885,22,23,33,24,993,93,96,97,98,99,911,912,913,914,915,331,43,34,44,53,35,54,45,55,99910,99920,99930,994,995,9910,9912,9913,9914,918,920,921,922,924,925,926,927,928,929,933,934,935,936,937,938,939,943,944,945,9916,9917,947,951,952,955,956,957};
    static const std::vector<int> S_g7 = {1,882,883,884,885,886,23,33,24,993,91,93,94,95,96,97,98,99,910,911,34,44,35,45,54,55,99920,99930,995,996,997,998,9911,9912,9913,9914,918,919,920,921,922,923,924,925,926,927,928,929,930,931,932,933,944,945,9915,9916,946,947,950,951,952,953,954};
    static const std::vector<int> S_g8 = {1,882,883,884,885,886,887,23,33,24,993,91,93,94,95,96,97,98,99,910,911,34,44,35,45,54,55,99920,99930,995,996,997,998,9911,9912,9913,9914,918,919,920,921,922,923,924,925,926,927,928,929,930,931,932,933,944,945,9915,9916,946,947,950,951,952,953,954};
    static const std::vector<int> S_g12 = {1,882,883,884,885,886,887,8881,889,8810,8811,24,93,94,95,96,35,45,55,99930,996,998,999,9912,918,919,920,921,922,923,945,9915,946,947,948,949,950};

    switch (gval){
        case 4: return S_g4;
        case 6: return S_g6;
        case 7: return S_g7;
        case 8: return S_g8;
        case 12: return S_g12;
        default: return dummy;
    }
}

static inline const std::vector<int>& allowed_I_params(int gval){
    static std::vector<int> dummy;
    static const std::vector<int> I_g4 = {1,882,883};
    static const std::vector<int> I_g6 = {1,882,883,884,885};
    static const std::vector<int> I_g7 = {1,882,883,884,885,886};
    static const std::vector<int> I_g8 = {1,882,883,884,885,886,887};
    static const std::vector<int> I_g12 = {1,882,883,884,885,886,887,8881,889,8810,8811};

    switch (gval){
        case 4: return I_g4;
        case 6: return I_g6;
        case 7: return I_g7;
        case 8: return I_g8;
        case 12: return I_g12;
        default: return dummy;
    }
}

// ========== ✨ ADDED: Topology to TheoryGraph conversion ==========
TheoryGraph topology_to_theory_graph(const Topology& T) {
    TheoryGraph G;
    
    if (T.block.empty()) return G;
    
    std::vector<NodeRef> nodes;
    nodes.reserve(T.block.size());
    
    for (const auto& b : T.block) {
        Spec sp;
        switch(b.kind) {
            case LKind::g: sp = n(b.param); break;
            case LKind::L: sp = i(b.param); break;
            case LKind::S: sp = s(b.param); break;
            case LKind::I: sp = s(b.param); break;
            default: sp = n(b.param); break;
        }
        nodes.push_back(G.add(sp));
    }
    
    std::vector<NodeRef> sideNodes;
    for (const auto& sl : T.side_links) {
        sideNodes.push_back(G.add(s(sl.param)));
    }
    
    std::vector<NodeRef> instNodes;
    for (const auto& inst : T.instantons) {
        instNodes.push_back(G.add(s(inst.param)));
    }
    
    for (const auto& conn : T.l_connection) {
        if (conn.u >= 0 && conn.u < (int)nodes.size() &&
            conn.v >= 0 && conn.v < (int)nodes.size()) {
            try {
                G.connect(nodes[conn.u], nodes[conn.v]);
            } catch (const std::exception&) {
                throw;
            }
        }
    }
    
    for (const auto& conn : T.s_connection) {
        if (conn.u >= 0 && conn.u < (int)nodes.size() &&
            conn.v >= 0 && conn.v < (int)sideNodes.size()) {
            try {
                G.connect(sideNodes[conn.v], nodes[conn.u]);
            } catch (const std::exception&) {
                throw;
            }
        }
    }
    
    for (const auto& conn : T.i_connection) {
        if (conn.u >= 0 && conn.u < (int)nodes.size() &&
            conn.v >= 0 && conn.v < (int)instNodes.size()) {
            try {
                G.connect(instNodes[conn.v], nodes[conn.u]);
            } catch (const std::exception&) {
                throw;
            }
        }
    }
    
    return G;
}

// ========== ✨ ADDED: Classification functions ==========
bool is_LST(const TheoryGraph& G) {
    // LST: Little String Theory
    // Condition: Exactly ONE zero eigenvalue, all others negative
    try {
        auto IF = G.ComposeIF_Gluing();
        if (IF.rows() == 0) return false;
        
        Tensor t;
        t.SetIF(IF);
        
        auto eigenvalues = t.GetEigenvalues();
        if (eigenvalues.size() == 0) return false;
        
        int zero_count = 0;
        int negative_count = 0;
        const double tol = 1e-8;
        
        for (int i = 0; i < eigenvalues.size(); i++) {
            if (std::abs(eigenvalues(i)) < tol) {
                zero_count++;
            } else if (eigenvalues(i) < -tol) {
                negative_count++;
            } else {
                return false;  // Positive eigenvalue
            }
        }
        
        return (zero_count == 1 && negative_count == eigenvalues.size() - 1);
    } catch (...) {
        return false;
    }
}

bool is_SCFT(const TheoryGraph& G) {
    // SCFT: Superconformal Field Theory
    // Condition: Negative definite (all eigenvalues negative)
    try {
        auto IF = G.ComposeIF_Gluing();
        if (IF.rows() == 0) return false;
        
        Tensor t;
        t.SetIF(IF);
        
        auto eigenvalues = t.GetEigenvalues();
        if (eigenvalues.size() == 0) return false;
        
        const double tol = 1e-8;
        
        for (int i = 0; i < eigenvalues.size(); i++) {
            if (eigenvalues(i) > -tol) {
                return false;  // Non-negative eigenvalue
            }
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

// ========== Sharding utilities (✨ MODIFIED: added category parameter) ==========
static std::string prefix_from(const Topology& T, int upto=4){
    std::string s; s.reserve(std::min<int>(upto, (int)T.block.size()));
    for (int i=0; i<(int)T.block.size() && i<upto; i++){
        switch (T.block[i].kind){
            case LKind::g: s.push_back('g'); break;
            case LKind::L: s.push_back('L'); break;
            case LKind::S: s.push_back('S'); break;
            case LKind::I: s.push_back('I'); break;
        }
    }
    return s.empty() ? "empty" : s;
}

static std::string shard_path_with_category(const Topology& T, const std::string& outdir,
                                            TargetSpec::PrefixMode mode, char kindTag, 
                                            int u, const std::string& category)
{
    const int len = (int)T.block.size();
    std::string pref = prefix_from(T, 4);
    const bool head = (u == 0);
    
    if (mode == TargetSpec::PrefixMode::Kind) 
        pref = std::string(1, kindTag) + pref;
    if (mode == TargetSpec::PrefixMode::HeadKind && head) 
        pref = std::string(1, kindTag) + pref;

    std::string dir = outdir + "/" + category + "/len-" + std::to_string(len);  // ✨ MODIFIED: added category subdir
    std::filesystem::create_directories(dir);
    return dir + "/" + pref + ".txt";
}

// ========== Parallel processing structure ==========
struct OutputBuffer {
    std::unordered_map<std::string, std::string> buffers;
    std::mutex mtx;
    std::atomic<size_t> total_size{0};

    void append(const std::string& path, const std::string& line) {
        std::lock_guard<std::mutex> lock(mtx);
        buffers[path] += line + '\n';
        total_size += line.size() + 1;
    }

    void flush_to_disk(const std::string& outDir) {
        std::lock_guard<std::mutex> lock(mtx);
        
        for (auto& [path, content] : buffers) {
            if (content.empty()) continue;
            
            std::filesystem::create_directories(std::filesystem::path(path).parent_path());
            std::ofstream fout(path, std::ios::app);
            fout.write(content.data(), content.size());
        }
        
        buffers.clear();
        total_size = 0;
    }

    size_t size() const { return total_size.load(); }
};

// ========== Worker pool ==========
class WorkerPool {
private:
    std::vector<std::thread> workers;
    std::queue<std::vector<Topology>> task_queue;
    std::mutex queue_mtx;
    std::condition_variable cv;
    std::atomic<bool> stop{false};
    std::atomic<long long> processed{0};
    std::atomic<long long> saved{0};
    
    OutputBuffer output_buffer;
    const std::string outDir;
    const TargetSpec& spec;

public:
    WorkerPool(int num_threads, const std::string& outDir_, const TargetSpec& spec_)
        : outDir(outDir_), spec(spec_)
    {
        for (int i = 0; i < num_threads; ++i) {
            workers.emplace_back([this]() { worker_thread(); });
        }
    }

    ~WorkerPool() {
        stop = true;
        cv.notify_all();
        for (auto& t : workers) {
            if (t.joinable()) t.join();
        }
        output_buffer.flush_to_disk(outDir);
    }

    void submit_batch(std::vector<Topology>&& batch) {
        {
            std::lock_guard<std::mutex> lock(queue_mtx);
            task_queue.push(std::move(batch));
        }
        cv.notify_one();
    }

    long long get_processed() const { return processed.load(); }
    long long get_saved() const { return saved.load(); }

    void flush_if_needed() {
        if (output_buffer.size() > BUFFER_SIZE) {
            output_buffer.flush_to_disk(outDir);
        }
    }

private:
    void worker_thread() {
        while (!stop) {
            std::vector<Topology> batch;
            
            {
                std::unique_lock<std::mutex> lock(queue_mtx);
                cv.wait(lock, [this]() { return stop || !task_queue.empty(); });
                
                if (stop && task_queue.empty()) break;
                if (task_queue.empty()) continue;
                
                batch = std::move(task_queue.front());
                task_queue.pop();
            }

            for (const auto& base : batch) {
                process_one(base);
            }
            
            processed += batch.size();
            flush_if_needed();
        }
    }

    void process_one(const Topology& base) {
        for (int u = 0; u < (int)base.block.size(); ++u) {
            if (base.block[u].kind != LKind::g) continue;
            if (!spec.all_nodes && !spec.nodes.count(u)) continue;

            const int gval = base.block[u].param;

            // S decoration
            if (spec.do_S) {
                const auto& Sbank = allowed_S_params(gval);
                for (int sp : Sbank) {
                    Topology t = base;
                    t.addDecoration(LKind::S, sp, u);
                    
                    // ✨ ADDED: Classify and save only LST/SCFT
                    try {
                        TheoryGraph G = topology_to_theory_graph(t);
                        std::string category;
                        
                        if (is_LST(G)) {
                            category = "LST";
                        } else if (is_SCFT(G)) {
                            category = "SCFT";
                        } else {
                            continue; // ✨ ADDED: Skip if not LST or SCFT
                        }
                        
                        std::string path = shard_path_with_category(t, outDir, spec.prefix, 'S', u, category);
                        std::string line = serialize_line_compact(t);
                        output_buffer.append(path, line);
                        saved++;
                    } catch (...) {
                        // Failed to classify - skip
                    }
                }
            }

            // I decoration
            if (spec.do_I) {
                const auto& Ibank = allowed_I_params(gval);
                int decoCount = 0;
                for (int ip : Ibank) {
                    if (decoCount >= MAX_DECO_PER_NODE) break;
                    
                    Topology t = base;
                    t.addDecoration(LKind::I, ip, u);
                    
                    // ✨ ADDED: Classify and save only LST/SCFT
                    try {
                        TheoryGraph G = topology_to_theory_graph(t);
                        std::string category;
                        
                        if (is_LST(G)) {
                            category = "LST";
                        } else if (is_SCFT(G)) {
                            category = "SCFT";
                        } else {
                            continue;  // ✨ ADDED: Skip if not LST or SCFT
                        }
                        
                        std::string path = shard_path_with_category(t, outDir, spec.prefix, 'I', u, category);
                        std::string line = serialize_line_compact(t);
                        output_buffer.append(path, line);
                        saved++;
                    } catch (...) {
                        // Failed to classify - skip
                    }
                    
                    ++decoCount;
                }
            }
        }
    }
};

// ========== Input processing ==========
static InFmt parse_infmt(const std::string& s){
    if (s == "db") return InFmt::DB;
    if (s == "line") return InFmt::Line;
    return InFmt::Auto;
}

static void parse_nodes_arg(const std::string& arg, TargetSpec& T){
    if (arg == "all") { T.all_nodes = true; return; }
    T.all_nodes = false;
    std::stringstream ss(arg);
    std::string tok;
    while (std::getline(ss, tok, ',')){
        if (tok == "head") { T.nodes.insert(0); continue; }
        if (!tok.empty()) {
            try {
                T.nodes.insert(std::stoi(tok));
            } catch (...) {
                std::cerr << "[warn] invalid node index: " << tok << "\n";
            }
        }
    }
}

static void parse_kinds_arg(const std::string& arg, TargetSpec& T){
    T.do_S = T.do_I = false;
    std::stringstream ss(arg);
    std::string tok;
    while (std::getline(ss, tok, ',')){
        if (tok == "S") T.do_S = true;
        if (tok == "I") T.do_I = true;
    }
}

static TargetSpec::PrefixMode parse_prefix_arg(const std::string& arg){
    if (arg == "kind") return TargetSpec::PrefixMode::Kind;
    if (arg == "head-kind") return TargetSpec::PrefixMode::HeadKind;
    return TargetSpec::PrefixMode::None;
}

// ========== Line file processing ==========
static long long process_line_file(const std::string& path, WorkerPool& pool){
    std::ifstream fin(path);
    if (!fin) {
        std::cerr << "[skip] cannot open " << path << "\n";
        return 0;
    }

    std::vector<Topology> batch;
    batch.reserve(BATCH_SIZE);
    long long total = 0;
    std::string line;

    while (std::getline(fin, line)) {
        if (line.empty()) continue;
        
        Topology T;
        if (!deserialize_line_compact(line, T)) continue;
        
        batch.push_back(std::move(T));
        
        if (batch.size() >= BATCH_SIZE) {
            pool.submit_batch(std::move(batch));
            batch.clear();
            batch.reserve(BATCH_SIZE);
            total += BATCH_SIZE;
            
            if (total % 50000 == 0) {
                std::cout << "Read " << total << " topologies...\r" << std::flush;
            }
        }
    }

    if (!batch.empty()) {
        total += batch.size();
        pool.submit_batch(std::move(batch));
    }

    return total;
}

static long long process_line_path(const std::string& inPath, WorkerPool& pool){
    long long total = 0;
    if (std::filesystem::is_directory(inPath)) {
        for (auto& e : std::filesystem::recursive_directory_iterator(inPath)) {
            if (e.is_regular_file() && e.path().extension() == ".txt") {
                std::cout << "Processing: " << e.path().filename() << "\n";
                total += process_line_file(e.path().string(), pool);
            }
        }
    } else {
        total += process_line_file(inPath, pool);
    }
    return total;
}

// ========== DB processing ==========
static long long process_db(const std::string& dbPath, WorkerPool& pool){
    TopologyDB db(dbPath);
    auto recs = db.loadAll();
    
    std::vector<Topology> batch;
    batch.reserve(BATCH_SIZE);
    long long total = 0;

    for (auto& rec : recs) {
        batch.push_back(std::move(rec.topo));
        
        if (batch.size() >= BATCH_SIZE) {
            pool.submit_batch(std::move(batch));
            batch.clear();
            batch.reserve(BATCH_SIZE);
            total += BATCH_SIZE;
            
            if (total % 50000 == 0) {
                std::cout << "Read " << total << " topologies...\r" << std::flush;
            }
        }
    }

    if (!batch.empty()) {
        total += batch.size();
        pool.submit_batch(std::move(batch));
    }

    return total;
}

// ========== Main ==========
int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " <input> <out_dir> "
                  << "[--in auto|db|line] "
                  << "[--nodes all|head|0,2,5] "
                  << "[--kinds S|I|S,I] "
                  << "[--prefix none|kind|head-kind] "
                  << "[--threads N]\n";
        std::cerr << "\nGenerates decorated topologies and saves only LST and SCFT.\n";
        return 1;
    }

    const std::string inPath = argv[1];
    const std::string outDir = argv[2];

    TargetSpec Tspec;
    InFmt inFmt = InFmt::Auto;
    int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;

    for (int i = 3; i < argc; i++) {
        std::string a = argv[i];
        auto need = [&](int idx) { return idx + 1 < argc; };
        
        if (a == "--nodes" && need(i)) { parse_nodes_arg(argv[++i], Tspec); continue; }
        if (a == "--kinds" && need(i)) { parse_kinds_arg(argv[++i], Tspec); continue; }
        if (a == "--prefix" && need(i)) { Tspec.prefix = parse_prefix_arg(argv[++i]); continue; }
        if (a == "--in" && need(i)) { inFmt = parse_infmt(argv[++i]); continue; }
        if (a == "--threads" && need(i)) { num_threads = std::stoi(argv[++i]); continue; }
    }

    std::filesystem::create_directories(outDir);

    std::cout << "Starting with " << num_threads << " threads...\n";
    std::cout << "Will save only LST and SCFT topologies.\n";
    
    WorkerPool pool(num_threads, outDir, Tspec);
    
    auto start = std::chrono::high_resolution_clock::now();
    long long input_count = 0;

    if (inFmt == InFmt::DB) {
        input_count = process_db(inPath, pool);
    } else if (inFmt == InFmt::Line) {
        input_count = process_line_path(inPath, pool);
    } else {
        if (std::filesystem::is_directory(inPath)) {
            input_count = process_line_path(inPath, pool);
        } else {
            try {
                input_count = process_db(inPath, pool);
            } catch (...) {
                input_count = process_line_path(inPath, pool);
            }
        }
    }

    std::cout << "\nWaiting for workers to finish...\n";
    while (pool.get_processed() < input_count) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "Processed: " << pool.get_processed() << " / " << input_count 
                  << " (Saved: " << pool.get_saved() << ")\r" << std::flush;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();

    std::cout << "\n\nCompleted!\n";
    std::cout << "Input: " << input_count << " topologies\n";
    std::cout << "Processed: " << pool.get_processed() << " topologies\n";
    std::cout << "Saved (LST/SCFT only): " << pool.get_saved() << " topologies\n";
    std::cout << "Time: " << duration << " seconds\n";
    std::cout << "Output dir: " << outDir << "\n";

    return 0;
}
