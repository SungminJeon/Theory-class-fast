// classify_topology.cpp (✨ MODIFIED: output files named after input files)
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <filesystem>

#include <Eigen/Dense>
#include "Topology.h"
#include "TopologyDB.hpp"
#include "TopoLineCompact.hpp"
#include "Theory.h"

// ===== 유틸 =====
static inline void ensure_linear_chain(const Topology& T,
                                       std::vector<InteriorStructure>& out_chain){
    if (!T.l_connection.empty()) { out_chain = T.l_connection; return; }
    const int n = (int)T.block.size();
    if (n <= 1) return;
    out_chain.reserve(n-1);
    for (int i=1; i<n; ++i) out_chain.push_back({i-1, i});
}

static inline void append_matrix_txt_batch(std::string& buf, const Eigen::MatrixXi& M){
    const int R = M.rows(), C = M.cols();
    for (int i=0;i<R;++i){
        for (int j=0;j<C;++j){
            if (j) buf.push_back(' ');
            buf += std::to_string(M(i,j));
        }
        buf.push_back('\n');
    }
    buf.push_back('\n');
}

static inline void flush_to_file(const std::string& path, const std::string& buf){
    if (buf.empty()) return;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream out(path, std::ios::app);
    if (!out) throw std::runtime_error("cannot open " + path);
    out.write(buf.data(), (std::streamsize)buf.size());
}

// ✨ ADDED: Extract base filename without extension
static inline std::string get_base_filename(const std::string& path){
    std::filesystem::path p(path);
    return p.stem().string();  // filename without extension
}

// ✨ ADDED: Get relative path from base directory and convert to safe filename
static inline std::string get_safe_output_name(const std::string& fullPath, 
                                                const std::string& baseDir){
    std::filesystem::path full(fullPath);
    std::filesystem::path base(baseDir);
    
    // Try to get relative path
    std::string rel_path;
    try {
        auto rel = std::filesystem::relative(full, base);
        rel_path = rel.string();
    } catch (...) {
        // If relative path fails, just use filename
        rel_path = full.filename().string();
    }
    
    // Replace directory separators and spaces with underscores
    std::string safe_name = rel_path;
    std::replace(safe_name.begin(), safe_name.end(), '/', '_');
    std::replace(safe_name.begin(), safe_name.end(), '\\', '_');
    std::replace(safe_name.begin(), safe_name.end(), ' ', '_');  // ✨ ADDED: handle spaces
    
    // Remove extension
    auto ext_pos = safe_name.find_last_of('.');
    if (ext_pos != std::string::npos) {
        safe_name = safe_name.substr(0, ext_pos);
    }
    
    return safe_name;
}

// ===== Topology -> TheoryGraph =====
struct GraphBuildResult {
    TheoryGraph G;
};

static GraphBuildResult build_graph_from_topology(const Topology& T){
    GraphBuildResult R;

    // 1) g/L 본체 노드
    std::vector<int> nodeIdx_gL(T.block.size(), -1);
    for (size_t i=0; i<T.block.size(); ++i){
        const auto& b = T.block[i];
        Spec sp;
        switch (b.kind){
            case LKind::g: sp = Spec{Kind::Node,         b.param}; break;
            case LKind::L: sp = Spec{Kind::InteriorLink, b.param}; break;
            case LKind::S: sp = Spec{Kind::SideLink,     b.param}; break;
            case LKind::I: sp = Spec{Kind::SideLink,     b.param}; break;
        }
        nodeIdx_gL[i] = R.G.add(sp).id;
    }

    // 2) S/I 장식 노드
    std::vector<int> nodeIdx_S(T.side_links.size(), -1);
    for (size_t i=0; i<T.side_links.size(); ++i)
        nodeIdx_S[i] = R.G.add(Spec{Kind::SideLink, T.side_links[i].param}).id;

    std::vector<int> nodeIdx_I(T.instantons.size(), -1);
    for (size_t i=0; i<T.instantons.size(); ++i)
        nodeIdx_I[i] = R.G.add(Spec{Kind::SideLink, T.instantons[i].param}).id;

    // 3) 연결 복원
    std::vector<InteriorStructure> chain;
    ensure_linear_chain(T, chain);

    for (auto e : chain)
        R.G.connect(NodeRef{nodeIdx_gL.at(e.u)}, NodeRef{nodeIdx_gL.at(e.v)});

    for (auto e : T.s_connection)
        R.G.connect(NodeRef{nodeIdx_S.at(e.v)}, NodeRef{nodeIdx_gL.at(e.u)});

    for (auto e : T.i_connection)
        R.G.connect(NodeRef{nodeIdx_I.at(e.v)}, NodeRef{nodeIdx_gL.at(e.u)});

    return R;
}

// ===== 판정 로직 (✨ UPDATED: 실제 고유값 기반으로 변경) =====
static inline bool is_scft_accurate(const Eigen::MatrixXi& IF, double tol=1e-8){
    Eigen::MatrixXd A = (-IF).cast<double>();
    A = 0.5*(A + A.transpose());
    
    // 실제 고유값 계산
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(A);
    if (solver.info() != Eigen::Success) return false;
    
    const auto& evals = solver.eigenvalues();
    
    // SCFT: 모든 고유값이 양수 (negative definite -IF = positive definite)
    for (int i = 0; i < evals.size(); ++i) {
        if (evals(i) < tol) {
            return false;  // Non-positive eigenvalue found
        }
    }
    
    return true;
}

static inline bool is_lst_accurate(const Eigen::MatrixXi& IF, double tol=1e-8){
    Eigen::MatrixXd A = (-IF).cast<double>();
    A = 0.5*(A + A.transpose());
    
    // 실제 고유값 계산
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(A);
    if (solver.info() != Eigen::Success) return false;
    
    const auto& evals = solver.eigenvalues();
    
    // 고유값 분류
    int pos = 0, zero = 0, neg = 0;
    for (int i = 0; i < evals.size(); ++i) {
        const double eval = evals(i);
        if (std::abs(eval) < tol) {
            ++zero;
        } else if (eval > tol) {
            ++pos;
        } else {
            ++neg;
        }
    }
    
    const int n = (int)evals.size();
    
    // LST: 정확히 1개의 0 고유값, 나머지는 모두 양수
    return (neg == 0 && zero == 1 && pos == n - 1);
}

// ===== 입력 처리 =====
enum class InFmt { Auto, DB, Line };
static InFmt parse_infmt(const std::string& s){
    if (s=="db")   return InFmt::DB;
    if (s=="line") return InFmt::Line;
    return InFmt::Auto;
}

// ✨ MODIFIED: process_line_file now takes base_name for output naming
static long long process_line_file(const std::string& path,
                                   const std::string& outDir,
                                   const std::string& base_name){
    std::ifstream fin(path);
    if (!fin){ std::cerr << "[skip] cannot open " << path << "\n"; return 0; }
    
    long long Nproc=0, Nscft=0, Nlst=0;
    
    // ✨ MODIFIED: Output files named after input file
    const std::string out_scft = outDir + "/" + base_name + "_IF_SCFT.txt";
    const std::string out_lst  = outDir + "/" + base_name + "_IF_LST.txt";
    
    std::string buf_scft; buf_scft.reserve(1<<22);
    std::string buf_lst;  buf_lst .reserve(1<<22);
    
    auto flush_all = [&](){
        flush_to_file(out_scft, buf_scft); buf_scft.clear();
        flush_to_file(out_lst,  buf_lst);  buf_lst.clear();
    };
    
    std::string line;
    while (std::getline(fin, line)){
        if (line.empty()) continue;
        Topology T;
        if (!deserialize_line_compact(line, T)) continue;
        
        try{
            auto R  = build_graph_from_topology(T);
            Eigen::MatrixXi IF = R.G.ComposeIF_Gluing();

            if (is_scft_accurate(IF)) { append_matrix_txt_batch(buf_scft, IF); ++Nscft; }
            else if (is_lst_accurate(IF)) { append_matrix_txt_batch(buf_lst, IF); ++Nlst; }

            if ((++Nproc % 2000)==0) flush_all();
        } catch (const std::exception& e){
            std::cerr << "[Error] " << e.what() << " on topology " << T.name << "\n";
        }
    }
    
    flush_all();
    
    std::cout << "File: " << base_name << " | Processed: " << Nproc
              << " | SCFT: " << Nscft << " | LST: " << Nlst << "\n";
    
    return Nproc;
}

// ✨ MODIFIED: process_line_path now handles each file separately with directory structure preserved
static long long process_line_path(const std::string& inPath,
                                   const std::string& outDir){
    long long total=0;
    if (std::filesystem::is_directory(inPath)){
        // ✨ MODIFIED: Use safe output name that includes directory structure
        for (auto& e : std::filesystem::recursive_directory_iterator(inPath)){
            if (e.is_regular_file() && e.path().extension()==".txt"){
                std::string safe_name = get_safe_output_name(e.path().string(), inPath);
                total += process_line_file(e.path().string(), outDir, safe_name);
            }
        }
    } else {
        std::string base_name = get_base_filename(inPath);
        total += process_line_file(inPath, outDir, base_name);
    }
    return total;
}

// ✨ MODIFIED: process_db_file with base_name parameter
static long long process_db_file(const std::string& dbPath,
                                const std::string& outDir,
                                const std::string& base_name){
    TopologyDB db(dbPath);
    
    long long Nproc=0, Nscft=0, Nlst=0;
    
    // ✨ MODIFIED: Output files named after input file
    const std::string out_scft = outDir + "/" + base_name + "_IF_SCFT.txt";
    const std::string out_lst  = outDir + "/" + base_name + "_IF_LST.txt";
    
    std::string buf_scft; buf_scft.reserve(1<<22);
    std::string buf_lst;  buf_lst .reserve(1<<22);
    
    auto flush_all = [&](){
        flush_to_file(out_scft, buf_scft); buf_scft.clear();
        flush_to_file(out_lst,  buf_lst);  buf_lst.clear();
    };
    
    for (auto& rec : db.loadAll()){
        try{
            auto R  = build_graph_from_topology(rec.topo);
            Eigen::MatrixXi IF = R.G.ComposeIF_Gluing();

            if (is_scft_accurate(IF)) { append_matrix_txt_batch(buf_scft, IF); ++Nscft; }
            else if (is_lst_accurate(IF)) { append_matrix_txt_batch(buf_lst, IF); ++Nlst; }

            if ((++Nproc % 2000)==0) flush_all();
        } catch (const std::exception& e){
            std::cerr << "[Error] " << e.what() << " on topology " << rec.topo.name << "\n";
        }
    }
    
    flush_all();
    
    std::cout << "File: " << base_name << " | Processed: " << Nproc
              << " | SCFT: " << Nscft << " | LST: " << Nlst << "\n";
    
    return Nproc;
}

// ===== 메인 =====
int main(int argc, char** argv){
    if (argc < 3){
        std::cerr << "usage: " << argv[0] << " <input_path_or_dir> <out_dir> [--in line|db|auto]\n";
        std::cerr << "  Output files will be named: <input_basename>_IF_SCFT.txt and <input_basename>_IF_LST.txt\n";
        return 1;
    }
    const std::string inPath = argv[1];
    const std::string outDir = argv[2];
    std::filesystem::create_directories(outDir);

    InFmt inFmt = InFmt::Auto;
    for (int i=3; i<argc; ++i){
        if (std::string(argv[i])=="--in" && i+1<argc){
            inFmt = parse_infmt(argv[++i]);
        }
    }

    long long total = 0;

    if (inFmt==InFmt::DB) {
        std::string base_name = get_base_filename(inPath);
        total = process_db_file(inPath, outDir, base_name);
    } else if (inFmt==InFmt::Line || std::filesystem::is_directory(inPath)
               || std::filesystem::path(inPath).extension()==".txt") {
        total = process_line_path(inPath, outDir);
    } else {
        try { 
            std::string base_name = get_base_filename(inPath);
            total = process_db_file(inPath, outDir, base_name); 
        }
        catch (...) { 
            total = process_line_path(inPath, outDir); 
        }
    }

    std::cout << "\nTotal processed: " << total << "\n";
    std::cout << "Output dir: " << outDir << "\n";
    return 0;
}
