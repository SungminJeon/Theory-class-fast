// classify_topology.cpp (fixed & fast)
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
    // Topology.h: l_connection 원소 타입은 InteriorStructure(u,v) :contentReference[oaicite:1]{index=1}
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

// ===== Topology -> TheoryGraph (참조 전달) =====
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
            case LKind::g: sp = Spec{Kind::Node,         b.param}; break;   // g -> Node
            case LKind::L: sp = Spec{Kind::InteriorLink, b.param}; break;   // L -> InteriorLink
            case LKind::S: sp = Spec{Kind::SideLink,     b.param}; break;   // 방어적 처리
            case LKind::I: sp = Spec{Kind::SideLink,     b.param}; break;   // 방어적 처리
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

    // 3) 연결 복원 (side Right -> node Left)
    std::vector<InteriorStructure> chain;
    ensure_linear_chain(T, chain);  // 자동 체인 복원

    for (auto e : chain)
        R.G.connect(NodeRef{nodeIdx_gL.at(e.u)}, NodeRef{nodeIdx_gL.at(e.v)});   // g/L 체인

    for (auto e : T.s_connection)   // Topology.h: s_connection 원소 타입은 SideLinkStructure(u:node, v:sidelink) :contentReference[oaicite:2]{index=2}
        R.G.connect(NodeRef{nodeIdx_S.at(e.v)}, NodeRef{nodeIdx_gL.at(e.u)});    // S -> g/L

    for (auto e : T.i_connection)   // Topology.h: i_connection 원소 타입은 InstantonStructure(u:node, v:instanton) :contentReference[oaicite:3]{index=3}
        R.G.connect(NodeRef{nodeIdx_I.at(e.v)}, NodeRef{nodeIdx_gL.at(e.u)});    // I -> g/L

    return R;
}

// ===== 빠른 판정 로직 =====
// SCFT: IF의 모든 고윳값 < 0  ⇔  (-IF) 가 양의 definite
static inline bool is_scft_fast(const Eigen::MatrixXi& IF){
    Eigen::MatrixXd A = (-IF).cast<double>();
    A = 0.5*(A + A.transpose());                  // 수치 대칭화
    Eigen::LLT<Eigen::MatrixXd> llt(A);
    return (llt.info()==Eigen::Success);
}

// LST: 정확히 하나만 0, 나머지는 모두 음수  ⇔  A=-IF 는 정확히 하나만 0, 나머지는 모두 양수
static inline bool is_lst_fast_strict(const Eigen::MatrixXi& IF, double tol=1e-10){
    Eigen::MatrixXd A = (-IF).cast<double>();
    A = 0.5*(A + A.transpose());
    Eigen::LDLT<Eigen::MatrixXd> ldlt(A);
    if (ldlt.info()!=Eigen::Success) return false;

    const auto D = ldlt.vectorD();
    int pos=0, zero=0, neg=0;
    for (int i=0;i<D.size();++i){
        const double d = D(i);
        if      (d >  tol) ++pos;
        else if (d < -tol) ++neg;
        else               ++zero;
    }
    const int n = (int)D.size();
    // 엄격: 나머지 전부 양수, 정확히 하나만 0, 음수는 0개
    return (neg==0 && zero==1 && pos==n-1);
}

// ===== 입력 처리 =====
enum class InFmt { Auto, DB, Line };
static InFmt parse_infmt(const std::string& s){
    if (s=="db")   return InFmt::DB;
    if (s=="line") return InFmt::Line;
    return InFmt::Auto;
}

static long long process_line_file(const std::string& path,
                                   const std::function<void(const Topology&)>& consume){
    std::ifstream fin(path);
    if (!fin){ std::cerr << "[skip] cannot open " << path << "\n"; return 0; }
    long long cnt=0; std::string line;
    while (std::getline(fin, line)){
        if (line.empty()) continue;
        Topology T;
        if (!deserialize_line_compact(line, T)) continue;
        consume(T);
        ++cnt;
    }
    return cnt;
}

static long long process_line_path(const std::string& inPath,
                                   const std::function<void(const Topology&)>& consume){
    long long total=0;
    if (std::filesystem::is_directory(inPath)){
        for (auto& e : std::filesystem::recursive_directory_iterator(inPath)){
            if (e.is_regular_file() && e.path().extension()==".txt")
                total += process_line_file(e.path().string(), consume);
        }
    } else {
        total += process_line_file(inPath, consume);
    }
    return total;
}

static long long process_db_file(const std::string& dbPath,
                                 const std::function<void(const Topology&)>& consume){
    TopologyDB db(dbPath);
    long long cnt = 0;
    for (auto& rec : db.loadAll()){
        consume(rec.topo);
        ++cnt;
    }
    return cnt;
}

// ===== 메인 =====
int main(int argc, char** argv){
    if (argc < 3){
        std::cerr << "usage: " << argv[0] << " <input_path_or_dir> <out_dir> [--in line|db|auto]\n";
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

    long long Nproc=0, Nscft=0, Nlst=0;

    // 배치 버퍼
    std::string buf_scft; buf_scft.reserve(1<<22);
    std::string buf_lst;  buf_lst .reserve(1<<22);
    auto flush_all = [&](){
        flush_to_file(outDir + "/IF_SCFT.txt", buf_scft); buf_scft.clear();
        flush_to_file(outDir + "/IF_LST.txt" , buf_lst ); buf_lst.clear();
    };

    auto consume = [&](const Topology& T){
        try{
            // 그래프 & IF 1회
            auto R  = build_graph_from_topology(T);
            Eigen::MatrixXi IF = R.G.ComposeIF_Gluing();   // 여기서만 1회

            // 빠른 판정
            if (is_scft_fast(IF)) { append_matrix_txt_batch(buf_scft, IF); ++Nscft; }
            else if (is_lst_fast_strict(IF)) { append_matrix_txt_batch(buf_lst, IF); ++Nlst; }

            if ((++Nproc % 2000)==0) flush_all();  // 주기적 배치 flush
        } catch (const std::exception& e){
            std::cerr << "[Error] " << e.what() << " on topology " << T.name << "\n";
        }
    };

    if (inFmt==InFmt::DB) {
        process_db_file(inPath, consume);
    } else if (inFmt==InFmt::Line || std::filesystem::is_directory(inPath)
               || std::filesystem::path(inPath).extension()==".txt") {
        process_line_path(inPath, consume);
    } else {
        try { process_db_file(inPath, consume); }
        catch (...) { process_line_path(inPath, consume); }
    }

    // 마지막 flush
    flush_all();

    std::cout << "Processed: " << Nproc
              << " | SCFT: " << Nscft
              << " | LST: "  << Nlst << "\n";
    std::cout << "Output dir: " << outDir << "\n";
    return 0;
}

