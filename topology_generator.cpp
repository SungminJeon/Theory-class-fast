#include <iostream>
#include <fstream>
#include <vector>
#include <string>
//#include "Tensor.h"
//#include "Theory.h"
#include "Topology.h"
#include "TopologyDB.hpp"
#include <filesystem>
#include <unordered_set>
#include "TopoLineCompact.hpp"


// ---- Added: input format switch for DB/Line/Auto
enum class InFmt { DB, Line, Auto };
static InFmt parse_infmt(const std::string& s){
    if (s=="db")   return InFmt::DB;
    if (s=="line") return InFmt::Line;
    return InFmt::Auto;
}




static constexpr int G_BANK[] = {4,6,7,8,12};

static constexpr int L_BANK[] = {
  11,22,33,44,55,331,32,23,42,24,43,34,53,35,54,45
};
static constexpr int S_BANK[] = {
  1, 882, 883,884,885,886,887,8881,889,8810,8811,
  22, 33, 44, 55, 331, 32, 23, 42, 24, 43, 34, 53, 35, 54, 45,
  991, 9920, 9902, 993,
  91, 92, 93, 94, 95, 96, 97, 98, 99, 910, 911, 912, 913, 914, 915, 916, 917,
  99910, 99901, 99920, 99902, 99930, 99903,
  994, 995, 996, 997, 998, 999, 9910, 9911, 9912, 9913, 9914,
  918, 919, 920, 921, 922, 923, 924, 925, 926, 927,
  928, 929, 930, 931, 932, 933, 934, 935, 936, 937,
  938, 939, 940, 941, 942, 943, 944, 945,
  9915, 9916, 9917,
  946, 947, 948, 949, 950, 951, 952, 953, 954, 955, 956, 957
};
static constexpr int I_BANK[] = {1, 882, 883,884,885,886,887,8881,889,8810,8811};


const int numS = (int)(sizeof(S_BANK)/sizeof(S_BANK[0]));
const int numI = (int)(sizeof(I_BANK)/sizeof(I_BANK[0]));
const int numG = (int)(sizeof(G_BANK)/sizeof(G_BANK[0]));
const int numL = (int)(sizeof(L_BANK)/sizeof(L_BANK[0]));





struct IntView {
  const int* data = nullptr;
  std::size_t size = 0;
  bool empty() const { return size==0; }
};

#define RET_ARR(...) do { \
  static constexpr int A__[] = { __VA_ARGS__ }; \
  return IntView{A__, std::size(A__)}; \
} while(0)

// =====================================================
// (타입, 파라미터) -> 허용 집합
// =====================================================
[[nodiscard]] inline IntView allowed(LKind k, int param)
{
  // ---------- g(kind) -> 허용 L ----------
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

  // ---------- L(kind) -> 허용 g ----------
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

  // ---------- S(kind) -> 허용 g ----------
  if (k==LKind::S){
    switch(param){
      // side links (S와 L 동일 규칙 구간)
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

      // alkali 2-links with no -5 curves
      case 991:  RET_ARR(4);
      case 9920: RET_ARR(4);
      case 9902: RET_ARR(4);
      case 993:  RET_ARR(6,7,8);
      case 91:   RET_ARR(8);
      case 92:   RET_ARR(4);
      case 93:   RET_ARR(4,6,7,8,12);
      case 94:   RET_ARR(7,8,12);
      case 95:   RET_ARR(7,8,12);
      case 96:   RET_ARR(6,7,8,12);
      case 97:   RET_ARR(4,6,7,8);
      case 98:   RET_ARR(4,6,7,8);
      case 99:   RET_ARR(6,7,8);
      case 910:  RET_ARR(7,8);
      case 911:  RET_ARR(6,7,8);
      case 912:  RET_ARR(4,6);
      case 913:  RET_ARR(6);
      case 914:  RET_ARR(6);
      case 915:  RET_ARR(4,6);
      case 916:  RET_ARR(4);
      case 917:  RET_ARR(4);

      // alkali 3-links with one -5 curve
      case 99910: RET_ARR(4,6);
      //case 99901: RET_ARR();
      case 99920: RET_ARR(6,7,8);
      //case 99902: RET_ARR(6,7,8);
      case 99930: RET_ARR(6,7,8,12);
      //case 99903: RET_ARR(6,7,8,12);
      case 994:   RET_ARR(6);
      case 995:   RET_ARR(6,7,8);
      case 996:   RET_ARR(7,8,12);
      case 997:   RET_ARR(7,8);
      case 998:   RET_ARR(7,8,12);
      case 999:   RET_ARR(12);
      case 9910:  RET_ARR(6);
      case 9911:  RET_ARR(7,8);
      case 9912:  RET_ARR(6,7,8,12);
      case 9913:  RET_ARR(4,6,7,8);
      case 9914:  RET_ARR(6,7,8);

      // alkali 1-links with one -5 curve
      case 918:  RET_ARR(6,7,8,12);
      case 919:  RET_ARR(7,8,12);
      case 920:  RET_ARR(6,7,8,12);
      case 921:  RET_ARR(6,7,8,12);
      case 922:  RET_ARR(6,7,8,12);
      case 923:  RET_ARR(7,8,12);
      case 924:  RET_ARR(4,6,7,8);
      case 925:  RET_ARR(4,6,7,8);
      case 926:  RET_ARR(6,7,8);
      case 927:  RET_ARR(4,6,7,8);
      case 928:  RET_ARR(4,6,7,8);
      case 929:  RET_ARR(4,6,7,8);
      case 930:  RET_ARR(7,8);
      case 931:  RET_ARR(7,8);
      case 932:  RET_ARR(7,8);
      case 933:  RET_ARR(6,7,8);
      case 934:  RET_ARR(4,6);
      case 935:  RET_ARR(6);
      case 936:  RET_ARR(4,6);
      case 937:  RET_ARR(4,6);
      case 938:  RET_ARR(4,6);
      case 939:  RET_ARR(6);
      case 940:  RET_ARR(4);
      case 941:  RET_ARR(4);
      case 942:  RET_ARR(4);
      case 943:  RET_ARR(4,6);
      case 944:  RET_ARR(6,7,8);
      case 945:  RET_ARR(6,7,8,12);

      // alkali 2-links with two -5 curves
      case 9915: RET_ARR(7,8,12);
      case 9916: RET_ARR(6,7,8);
      case 9917: RET_ARR(6);

      // alkali 1-links with two -5 curves
      case 946:  RET_ARR(7,8,12);
      case 947:  RET_ARR(6,7,8,12);
      case 948:  RET_ARR(12);
      case 949:  RET_ARR(12);
      case 950:  RET_ARR(7,8,12);
      case 951:  RET_ARR(6,7,8);
      case 952:  RET_ARR(6,7,8);
      case 953:  RET_ARR(7,8);
      case 954:  RET_ARR(7,8);
      case 955:  RET_ARR(6);
      case 956:  RET_ARR(4,6);
      case 957:  RET_ARR(6);

      // instantons (S)
      case 1:     RET_ARR(4,6,7,8,12);
      case 882:   RET_ARR(4,6,7,8,12);
      case 883:   RET_ARR(4,6,7,8,12);
      case 884:   RET_ARR(6,7,8,12);
      case 885:   RET_ARR(6,7,8,12);
      case 886:   RET_ARR(7,8,12);
      case 887:   RET_ARR(8,12);
      case 8881:  RET_ARR(12);
      case 889:   RET_ARR(12);
      case 8810:  RET_ARR(12);
      case 8811:  RET_ARR(12);

      default: return {};
    }
  }

  // ---------- I(kind) -> 허용 g (instantons 동일 규칙) ----------
  if (k==LKind::I){
    switch(param){
      case 1:     RET_ARR(4,6,7,8,12);
      case 882:   RET_ARR(4,6,7,8,12);
      case 883:   RET_ARR(4,6,7,8,12);
      case 884:   RET_ARR(6,7,8,12);
      case 885:   RET_ARR(6,7,8,12);
      case 886:   RET_ARR(7,8,12);
      case 887:   RET_ARR(8,12);
      case 8881:  RET_ARR(12);
      case 889:   RET_ARR(12);
      case 8810:  RET_ARR(12);
      case 8811:  RET_ARR(12);
      default: return {};
    }
  }

  return {};
}

static std::unordered_set<std::string> g_seen_lines;

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


static std::string shard_path(const Topology& T, const std::string& outdir) {
	const int len = (int)T.block.size();
	const std::string pref = prefix_from(T,4);
	std::string dir = outdir + "/len-" + std::to_string(len);
	std::filesystem::create_directories(dir);
	return dir + "/" + pref + ".txt";
}
// g-파라미터들만 뽑아서 비엄격 단봉(≤ ... ≤ ≥ ... ≥)인지 검사
template<class T>
static inline bool is_unimodal_non_strict(const std::vector<T>& a){
    const int n = (int)a.size();
    if (n <= 2) return true;
    int i = 1;
    while (i < n && a[i] >= a[i-1]) ++i; // 비내림 구간
    while (i < n && a[i] <= a[i-1]) ++i; // 비내림 이후 비내림(내림 포함)
    return i == n;
}

// Topology에서 g만 추출해 단봉(prefix 포함) 조건 확인
static inline bool g_unimodal_prefix_ok(const Topology& T){
    std::vector<int> gvals; gvals.reserve(T.block.size());
    for (const auto& b : T.block){
        if (b.kind == LKind::g) gvals.push_back(b.param);
    }
    return is_unimodal_non_strict(gvals);
}

static inline void save_one_compact(const Topology& T, const std::string& outdir){
	const std::string line = serialize_line_compact(T);
	if (!g_seen_lines.insert(line).second) return; 
	const std::string path = shard_path(T,outdir);
	std::ofstream fout(path, std::ios::app);
	fout << line << '\n';
}




static int generate_one_step(const Topology& base, const std::string& outDir)
{
    if (base.block.empty()) return 0;

    if (!g_unimodal_prefix_ok(base)) return 0;
    const auto& last = base.block.back();
    const LKind k = last.kind;
    const int   p = last.param;

    // g 또는 L만 확장 허용
    if (!(k == LKind::g || k == LKind::L)) return 0;

    const IntView opts = allowed(k, p);
    if (opts.empty()) return 0;

    int saved = 0;
    for (std::size_t i = 0; i < opts.size; ++i) {
        Topology t = base;
        const int nextParam = opts.data[i];

        // g 다음은 L, L 다음은 g
        const LKind nextKind = (k == LKind::g ? LKind::L : LKind::g);
        t.addBlockRight(nextKind, nextParam);  // 내부 링크 자동 연결됨 (newId-1,newId)
        // 이름은 굳이 저장 안 함. 필요한 경우 주석 해제
        // t.name.clear();

	if (!g_unimodal_prefix_ok(t)) continue;
        save_one_compact(t, outDir); ++saved;
    }
    return saved;
}

// ----------------------------------------------------
// 입력 DB 전체 레코드를 읽어 각 레코드마다 1스텝 확장
// ----------------------------------------------------
static int expand_db_one_step(const TopologyDB& inDB, const std::string& outDir)
{
    int total = 0;
    auto recs = inDB.loadAll();  // name, topo 쌍 목록
				 //

    for (const auto& r : recs) {
        total += generate_one_step(r.topo, outDir);
    }
    return total;
}

// ---- Added: process line-compact inputs (file or directory)
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

// ----------------------------------------------------
int main(int argc, char** argv)
{
    if (argc < 3){
        std::cerr << "usage: " << argv[0] << " <input> <out_dir> [--in db|line|auto]\n";
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
    std::cout << "Generated " << saved << " topologies into " << outPath
              << " (line-compact, sharded)\n";
    return 0;
}

