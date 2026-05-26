// 原始main函数
#include "common/BOBHash.hh"
#include "common/test.hh"
#include "sketch/FDFilter.hh"
#include "sketch/MergeableFDFilter.hh"
#include "sketch/GAgingFDFilter.hh"
#include "sketch/GAgingMergeableFDFilter.hh"
#include "sketch/OptFDFilter.hh"
#include "sketch/OptMergeableFDFilter.hh"
#include "sketch/LFFilter.hh"     //新增LF-Filter数据结构
// #include "sketch/LFFilter-pdc.hh"     //Predictor内存优化
#include <algorithm>
#include <string>
#include <vector>
using std::string;
using std::vector;

// LF-Filter结构测试函数声明
void testLFFilter(std::shared_ptr<INIReader> config, 
                        const vector<util::Record> &records);

void testFDFilter(std::shared_ptr<INIReader> config,
                        const vector<util::Record> &records);
void testMergeableFDFilter(std::shared_ptr<INIReader> config,
                const vector<util::Record> &records);
void testOptFDFilter(std::shared_ptr<INIReader> config,
                        const vector<util::Record> &records);
void testOptMergeableFDFilter(std::shared_ptr<INIReader> config,
                        const vector<util::Record> &records);
void testGAgingFDFilter(std::shared_ptr<INIReader> config,
                        const vector<util::Record> &records);
void testGAgingMergeableFDFilter(std::shared_ptr<INIReader> config,
                const vector<util::Record> &records);


int main(int argc, char *argv[]) {

  auto config = util::read_ini("../config.ini");
  string data_file = config->Get("COMMON", "data_file", "");
  auto records = util::readTrace(data_file);
  std::set<FlowKey<13>> s;
  std::transform(records.begin(), records.end(), std::inserter(s, s.begin()),
                 [](auto &record) { return record.flowkey_; });
  printf("flow num is %ld \n", s.size());
  
  // --- 添加这一行 ---
  testLFFilter(config, records);
  // testFDFilter(config, records);
  // testMergeableFDFilter(config, records);


  // testGAgingMergeableFDFilter(config, records);
  // testGAgingFDFilter(config, records);

  // testOptFDFilter(config, records);
  // testOptMergeableFDFilter(config, records);
  
  return 0;
}

// 在 main.cc 中添加此函数，测试LF-Filter结构是否生效
void testLFFilter(std::shared_ptr<INIReader> config, const vector<util::Record> &records) {
    std::cout << "\n>>> Testing LF-Filter <<<" << std::endl;
    
    // 从配置读取阈值
    uint64_t delay_thres = config->GetInteger("COMMON", "delay_thres", 600000);
    uint64_t jitter_thres = config->GetInteger("COMMON", "jitter_thres", 100000);
    
    // 参数配置 (参考论文与内存限制)
    int n_hot = 5;
    int n_warm = 10;
    int n_cold = 15;
    int k_w = 3;         // 哈希次数
    int heavy_thres = 2;
    int slots = 250000;   // 过滤器大小 (控制内存)
    int fp_bits = 7;     // 指纹位数
    int win_id_bits = 2; // Window ID 位数
    
    // 实例化
    sketch::LFFilter<hash::AwareHash> lff(n_hot, n_warm, n_cold, k_w, slots, fp_bits, win_id_bits, delay_thres, heavy_thres);
    // sketch::LFFilter<hash::BOBHash32> lff(n_hot, n_warm, n_cold, k_w, slots, fp_bits, win_id_bits, delay_thres, heavy_thres);
    // sketch::LFFilter<hash::AwareHash> lff(
    //     n_hot, n_warm, n_cold, k_w, 
    //     slots, slots*12, slots, slots,
    //     fp_bits, win_id_bits, delay_thres, heavy_thres 
    // );

    // 【新增】打印四大模块的内存分布
    lff.printMemoryBreakdown();
    
    // 调用通用测试框架
    standardTest(lff, records, delay_thres, jitter_thres);

    // 【新增】打印预测器性能
    // lff.printPredictorStats();
}

void testFDFilter(std::shared_ptr<INIReader> config,
                    const vector<util::Record> &records) {
  int nbits = config->GetInteger("FDFilter", "nbits", 0);
  int num_hash = config->GetInteger("FDFilter", "num_hash", 0);
  int gnits = config->GetInteger("FDFilter", "gnbits", 0);
  int gnum_hash = config->GetInteger("FDFilter", "gnum_hash", 0);
  int k = config->GetInteger("FDFilter", "k", 0);
  int kk = config->GetInteger("FDFilter", "kk", 0);
  int block_size = 4;
  nbits = nbits * (2*block_size) / (2*block_size+1);
  uint64_t delay_thres = config->GetInteger("COMMON", "delay_thres", 0);
  uint64_t jitter_thres = config->GetInteger("COMMON", "jitter_thres", 0);
  sketch::FDFilter<hash::AwareHash> mbbf{k, kk, nbits, num_hash,
                                           gnits, gnum_hash, delay_thres};
  standardTest(mbbf, records, delay_thres, jitter_thres);
}
void testGAgingMergeableFDFilter(std::shared_ptr<INIReader> config,
                const vector<util::Record> &records) {
  int nbits = config->GetInteger("MergeableFDFilter", "nbits", 0);
  int num_hash = config->GetInteger("MergeableFDFilter", "num_hash", 0);
  int gnits = config->GetInteger("MergeableFDFilter", "gnbits", 0);
  int gnum_hash = config->GetInteger("MergeableFDFilter", "gnum_hash", 0);
  int k = config->GetInteger("MergeableFDFilter", "k", 0);
  int kk = config->GetInteger("MergeableFDFilter", "kk", 0);
  int block_size = 8;
  //gnits = gnits * block_size/(block_size+1);
  uint64_t delay_thres = config->GetInteger("COMMON", "delay_thres", 0);
  uint64_t jitter_thres = config->GetInteger("COMMON", "jitter_thres", 0);
  sketch::MergeableFDFilter<hash::AwareHash> mbbf{k, kk, nbits, num_hash,
                                           gnits, gnum_hash, delay_thres};
  standardTest(mbbf, records, delay_thres, jitter_thres);
}

void testGAgingFDFilter(std::shared_ptr<INIReader> config,
                    const vector<util::Record> &records) {
  int nbits = config->GetInteger("FDFilter", "nbits", 0);
  int num_hash = config->GetInteger("FDFilter", "num_hash", 0);
  int gnits = config->GetInteger("FDFilter", "gnbits", 0);
  int gnum_hash = config->GetInteger("FDFilter", "gnum_hash", 0);
  int k = config->GetInteger("FDFilter", "k", 0);
  int kk = config->GetInteger("FDFilter", "kk", 0);
  uint64_t delay_thres = config->GetInteger("COMMON", "delay_thres", 0);
  uint64_t jitter_thres = config->GetInteger("COMMON", "jitter_thres", 0);
  int block_size = 32;
  //gnits = gnits * block_size/(block_size+1);
  sketch::GAgingFDFilter<hash::AwareHash> mbbf{k, kk, nbits, num_hash,
                                           gnits, gnum_hash, delay_thres, block_size, 1112458/4};
  standardTest(mbbf, records, delay_thres, jitter_thres);
}

void testMergeableFDFilter(std::shared_ptr<INIReader> config,
                const vector<util::Record> &records) {
  int nbits = config->GetInteger("MergeableFDFilter", "nbits", 0);
  int num_hash = config->GetInteger("MergeableFDFilter", "num_hash", 0);
  int gnits = config->GetInteger("MergeableFDFilter", "gnbits", 0);
  int gnum_hash = config->GetInteger("MergeableFDFilter", "gnum_hash", 0);
  int k = config->GetInteger("MergeableFDFilter", "k", 0);
  int kk = config->GetInteger("MergeableFDFilter", "kk", 0);
  int block_size = 4;
  nbits = nbits * (2*block_size) / (2*block_size+1);
  uint64_t delay_thres = config->GetInteger("COMMON", "delay_thres", 0);
  uint64_t jitter_thres = config->GetInteger("COMMON", "jitter_thres", 0);
  sketch::MergeableFDFilter<hash::AwareHash> mbbf{k, kk, nbits, num_hash,
                                           gnits, gnum_hash, delay_thres};
  standardTest(mbbf, records, delay_thres, jitter_thres);
}

void testOptFDFilter(std::shared_ptr<INIReader> config,
                        const vector<util::Record> &records) {
  int nbits = config->GetInteger("FDFilter", "nbits", 0);
  int num_hash = config->GetInteger("FDFilter", "num_hash", 0);
  int gnits = config->GetInteger("FDFilter", "gnbits", 0);
  int gnum_hash = config->GetInteger("FDFilter", "gnum_hash", 0);
  int k = config->GetInteger("FDFilter", "k", 0);
  int kk = config->GetInteger("FDFilter", "kk", 0);
  uint64_t delay_thres = config->GetInteger("COMMON", "delay_thres", 0);
  uint64_t jitter_thres = config->GetInteger("COMMON", "jitter_thres", 0);
  sketch::OptFDFilter<hash::AwareHash> ombbf{k, kk, nbits, num_hash,
                                           gnits, gnum_hash, delay_thres};
  standardTest(ombbf, records, delay_thres, jitter_thres);
}

void testOptMergeableFDFilter(std::shared_ptr<INIReader> config,
                        const vector<util::Record> &records) {
  int nbits = config->GetInteger("FDFilter", "nbits", 0);
  int num_hash = config->GetInteger("FDFilter", "num_hash", 0);
  int gnits = config->GetInteger("FDFilter", "gnbits", 0);
  int gnum_hash = config->GetInteger("FDFilter", "gnum_hash", 0);
  int k = config->GetInteger("FDFilter", "k", 0);
  int kk = config->GetInteger("FDFilter", "kk", 0);
  uint64_t delay_thres = config->GetInteger("COMMON", "delay_thres", 0);
  uint64_t jitter_thres = config->GetInteger("COMMON", "jitter_thres", 0);
  sketch::OptMergeableFDFilter<hash::AwareHash> ombbf{k, kk, nbits, num_hash,
                                           gnits, gnum_hash, delay_thres};
  standardTest(ombbf, records, delay_thres, jitter_thres);
}
