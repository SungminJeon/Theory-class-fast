#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include "Topology.h"
#include "TopologyDB.hpp"
#include "Theory.h"
#include <filesystem>
#include <unordered_set>
#include "TopoLineCompact.hpp"

// ========== Input format enum ==========
enum class InFmt { DB, Line, Auto };
static InFmt parse_infmt(const std::string& s){
    if (s=="db")   return InFmt::DB;
    if (s=="line") return InFmt::Line;
    return InFmt::Auto;
}

// ========== Banks (same as original) ==========
static constexpr int G_BANK[] = {4,6,7,8,12};
static constexpr int L_BANK[] = {
  11,22,33,44,55,331,32,23,42,24,43,34,53,35,54,45
};

const int numG = (int)(sizeof(G_BANK)/sizeof(G_BANK[0]));
const int numL = (int)(sizeof(L_BANK)/sizeof(L_BANK[0]));

// ========== Gluing rules ==========
struct IntView {
  const int* data = nullptr;
  std::size_t size = 0;
  bool empty() const { return size==0; }
};

#define RET_ARR(...) do { \
  static constexpr int A__[] = { __VA_ARGS__ }; \
  return IntView{A__, std::size(A__)}; \
} while(0)

[[nodiscard]] inline IntView allowed(LKind k, int param)
{
  if (k==LKind::g){
    switch(param){
      case 4:  RET_ARR(11,22,32,23,33,24,331,34,35);
      case 6:  RET_ARR(22,32,33,42,331,43,34,44,53,35,45,54,55);
      case 7:  RET_ARR(32,33,42,43,44,53,54,45,55);
      case 8:  RET_ARR(32,33,42,43,44,53,54,45,55);
      case 12: RET_ARR(42,53,54,55);
      default: return {};
    }
  }

  if (k==LKind::L){
    switch(param){
      case 11:  RET_ARR(4);
      case 22:  RET_ARR(4,6);
      case 32:  RET_ARR(4);
      case 23:  RET_ARR(4,6,7,8);
      case 33:  RET_ARR(4,6,7,8);
      case 42:  RET_ARR(4);
      case 24:  RET_ARR(6,7,8,12);
      case 331: RET_ARR(4,6);
      case 43:  RET_ARR(4,6);
      case 34:  RET_ARR(6,7,8);
      case 44:  RET_ARR(6,7,8);
      case 53:  RET_ARR(4,6);
      case 35:  RET_ARR(6,7,8,12);
      case 54:  RET_ARR(6,7,8);
      case 45:  RET_ARR(6,7,8,12);
      case 55:  RET_ARR(6,7,8,12);
      default: return {};
    }
  }

  return {};
}

// ========== ✨ ADDED: Topology to TheoryGraph conversion ==========
TheoryGraph topology_to_theory_graph(const Topology& T) {
    TheoryGraph G;
    
    if (T.block.empty()) return G;
    
    // Map block indices to NodeRef
    std::vector<NodeRef> nodes;
    nodes.reserve(T.block.size());
    
    // Add all blocks as nodes
    for (const auto& b : T.block) {
        Spec sp;
        switch(b.kind) {
            case LKind::g: sp = n(b.param); break;
            case LKind::L: sp = i(b.param); break;
            case LKind::S: sp = s(b.param); break;
            case LKind::I: sp = s(b.param); break; // Instantons treated as sidelinks
            default: sp = n(b.param); break;
        }
        nodes.push_back(G.add(sp));
    }
    
    // Add side links
    std::vector<NodeRef> sideNodes;
    for (const auto& sl : T.side_links) {
        sideNodes.push_back(G.add(s(sl.param)));
    }
    
    // Add instantons
    std::vector<NodeRef> instNodes;
    for (const auto& inst : T.instantons) {
        instNodes.push_back(G.add(s(inst.param)));
    }
    
    // Connect interior links (l_connection)
    for (const auto& conn : T.l_connection) {
        if (conn.u >= 0 && conn.u < (int)nodes.size() &&
            conn.v >= 0 && conn.v < (int)nodes.size()) {
            try {
                G.connect(nodes[conn.u], nodes[conn.v]);
            } catch (const std::exception& e) {
                // Connection failed due to gluing rules - skip this topology
                throw;
            }
        }
    }
    
    // Connect side links (s_connection)
    for (const auto& conn : T.s_connection) {
        if (conn.u >= 0 && conn.u < (int)nodes.size() &&
            conn.v >= 0 && conn.v < (int)sideNodes.size()) {
            try {
                G.connect(sideNodes[conn.v], nodes[conn.u]);
            } catch (const std::exception& e) {
                throw;
            }
        }
    }
    
    // Connect instantons (i_connection)
    for (const auto& conn : T.i_connection) {
        if (conn.u >= 0 && conn.u < (int)nodes.size() &&
            conn.v >= 0 && conn.v < (int)instNodes.size()) {
            try {
                G.connect(instNodes[conn.v], nodes[conn.u]);
            } catch (const std::exception& e) {
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
    // This corresponds to signature (0, T-1, 1) where 1 is the null direction
    try {
        auto IF = G.ComposeIF_Gluing();
        if (IF.rows() == 0) return false;
        
        Tensor t;
        t.SetIF(IF);
        
        auto eigenvalues = t.GetEigenvalues();
        if (eigenvalues.size() == 0) return false;
        
        // Count zero and negative eigenvalues
        int zero_count = 0;
        int negative_count = 0;
        const double tol = 1e-8;
        
        for (int i = 0; i < eigenvalues.size(); i++) {
            if (std::abs(eigenvalues(i)) < tol) {
                zero_count++;
            } else if (eigenvalues(i) < -tol) {
                negative_count++;
            } else {
                // Positive eigenvalue found - not LST
                return false;
            }
        }
        
        // LST: exactly 1 zero eigenvalue, rest negative
        return (zero_count == 1 && negative_count == eigenvalues.size() - 1);
    } catch (...) {
        return false;
    }
}

bool is_SCFT(const TheoryGraph& G) {
    // SCFT: Superconformal Field Theory
    // Condition: Negative definite intersection form (all eigenvalues negative)
    // No zero eigenvalues, no positive eigenvalues
    try {
        auto IF = G.ComposeIF_Gluing();
        if (IF.rows() == 0) return false;
        
        Tensor t;
        t.SetIF(IF);
        
        auto eigenvalues = t.GetEigenvalues();
        if (eigenvalues.size() == 0) return false;
        
        const double tol = 1e-8;
        
        // All eigenvalues must be negative
        for (int i = 0; i < eigenvalues.size(); i++) {
            if (eigenvalues(i) > -tol) {
                // Non-negative eigenvalue found - not SCFT
                return false;
            }
        }
        
        // All eigenvalues are negative - this is SCFT
        return true;
    } catch (...) {
        return false;
    }
}

bool is_SUGRA(const TheoryGraph& G) {
    try {
        auto IF = G.ComposeIF_Gluing();
        if (IF.rows() == 0) return false;
        
        Tensor t;
        t.SetIF(IF);
        return t.IsSUGRA();
    } catch (...) {
        return false;
    }
}

// ========== Deduplication ==========
static std::unordered_set<std::string> g_seen_lines;

// ========== Sharding utilities (✨ MODIFIED: added category parameter) ==========
static std::string prefix_from(const Topology& T, int upto=4){
    std::string s; 
    s.reserve(std::min<int>(upto, (int)T.block.size()));
    
    for (int i=0; i< (int)T.block.size() && i < upto; i++) {
        switch (T.block[i].kind){
            case LKind::g: s.push_back('g'); break;
            case LKind::L: s.push_back('L'); break;
            case LKind::S: s.push_back('S'); break;
            case LKind::I: s.push_back('I'); break;
        }
    }
    if (s.empty()) s = "empty";
    return s;
}

static std::string shard_path(const Topology& T, const std::string& outdir, const std::string& category) {
    const int len = (int)T.block.size();
    const std::string pref = prefix_from(T,4);
    std::string dir = outdir + "/" + category + "/len-" + std::to_string(len);  // ✨ MODIFIED: added category subdir
    std::filesystem::create_directories(dir);
    return dir + "/" + pref + ".txt";
}

// ========== Unimodal check ==========
template<class T>
static inline bool is_unimodal_non_strict(const std::vector<T>& a){
    const int n = (int)a.size();
    if (n <= 2) return true;
    int i = 1;
    while (i < n && a[i] >= a[i-1]) ++i;
    while (i < n && a[i] <= a[i-1]) ++i;
    return i == n;
}

static inline bool g_unimodal_prefix_ok(const Topology& T){
    std::vector<int> gvals; gvals.reserve(T.block.size());
    for (const auto& b : T.block){
        if (b.kind == LKind::g) gvals.push_back(b.param);
    }
    return is_unimodal_non_strict(gvals);
}

// ========== ✨ MODIFIED: Save with classification (only LST/SCFT) ==========
static inline void save_one_compact_classified(const Topology& T, const std::string& outdir){
    const std::string line = serialize_line_compact(T);
    if (!g_seen_lines.insert(line).second) return; 
    
    // ✨ ADDED: Convert to TheoryGraph and classify
    try {
        TheoryGraph G = topology_to_theory_graph(T);
        
        std::string category = "other";
        if (is_LST(G)) {
            category = "LST";
        } else if (is_SCFT(G)) {
            category = "SCFT";
        }
        // ✨ ADDED: Only save LST or SCFT
        else {
            return; // Skip this topology
        }
        
        const std::string path = shard_path(T, outdir, category);
        std::ofstream fout(path, std::ios::app);
        fout << line << '\n';
    } catch (const std::exception& e) {
        // Failed to convert or classify - skip
        return;
    }
}

// ========== Generation ==========
static int generate_one_step(const Topology& base, const std::string& outDir)
{
    if (base.block.empty()) return 0;

    if (!g_unimodal_prefix_ok(base)) return 0;
    const auto& last = base.block.back();
    const LKind k = last.kind;
    const int   p = last.param;

    // Only extend g or L
    if (!(k == LKind::g || k == LKind::L)) return 0;

    const IntView opts = allowed(k, p);
    if (opts.empty()) return 0;

    int saved = 0;
    for (std::size_t i = 0; i < opts.size; ++i) {
        Topology t = base;
        const int nextParam = opts.data[i];

        const LKind nextKind = (k == LKind::g ? LKind::L : LKind::g);
        t.addBlockRight(nextKind, nextParam);

        if (!g_unimodal_prefix_ok(t)) continue;
        save_one_compact_classified(t, outDir); ++saved;
    }
    return saved;
}

// ========== Process DB ==========
static int expand_db_one_step(const TopologyDB& inDB, const std::string& outDir)
{
    int total = 0;
    auto recs = inDB.loadAll();

    for (const auto& r : recs) {
        total += generate_one_step(r.topo, outDir);
    }
    return total;
}

// ========== Process line files ==========
static long long process_line_file(const std::string& path, const std::string& outDir){
    std::ifstream fin(path);
    if (!fin){ std::cerr << "[skip] cannot open " << path << "\n"; return 0; }
    long long made = 0; std::string line;
    while (std::getline(fin, line)){
        if (line.empty()) continue;
        Topology T;
        if (!deserialize_line_compact(line, T)) continue;
        made += generate_one_step(T, outDir);
    }
    return made;
}

static long long process_line_path(const std::string& inPath, const std::string& outDir){
    long long total = 0;
    if (std::filesystem::is_directory(inPath)){
        for (auto& e: std::filesystem::recursive_directory_iterator(inPath)){
            if (e.is_regular_file() && e.path().extension()==".txt")
                total += process_line_file(e.path().string(), outDir);
        }
    } else {
        total += process_line_file(inPath, outDir);
    }
    return total;
}

// ========== Main ==========
int main(int argc, char** argv)
{
    if (argc < 3){
        std::cerr << "usage: " << argv[0] << " <input> <out_dir> [--in db|line|auto]\n";
        std::cerr << "  Generates topologies and saves only LST and SCFT to line-compact format\n";
        return 1;
    }
    std::string inPath  = argv[1];
    std::string outPath = argv[2];
    InFmt inFmt = InFmt::Auto;
    for (int i=3;i<argc;i++){
        if (std::string(argv[i])=="--in" && i+1<argc) inFmt = parse_infmt(argv[++i]);
    }
    std::filesystem::create_directories(outPath);

    int saved = 0;
    if (inFmt==InFmt::DB){
        TopologyDB inDB(inPath);
        saved = expand_db_one_step(inDB, outPath);
    } else if (inFmt==InFmt::Line){
        saved = (int)process_line_path(inPath, outPath);
    } else { // Auto
        if (std::filesystem::is_directory(inPath)){
            saved = (int)process_line_path(inPath, outPath);
        } else {
            try {
                TopologyDB inDB(inPath);
                saved = expand_db_one_step(inDB, outPath);
            } catch (...) {
                saved = (int)process_line_path(inPath, outPath);
            }
        }
    }
    std::cout << "Generated " << saved << " LST/SCFT topologies into " << outPath
              << " (line-compact, sharded by category)\n";
    return 0;
}
