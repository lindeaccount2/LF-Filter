#include "common/BOBHash.hh"
#include "common/test.hh"
#include "sketch/FDFilter.hh"
#include "sketch/LFFilter.hh"     //新增LF-Filter数据结构
#include "sketch/LFFilter_ablation.hh"     //Predictor内存优化
#include <algorithm>
#include <string>
#include <vector>
#include <iostream>
#include <chrono>    // 【必须】用于计时
#include <iomanip>   // 【推荐】用于格式化输出时间
using std::string;
using std::vector;

// LF-Filter结构测试函数声明
void testLFFilter(std::shared_ptr<INIReader> config, 
                        const vector<util::Record> &records);

void testFDFilter(std::shared_ptr<INIReader> config,
                        const vector<util::Record> &records);
// --- [新增] 消融实验函数声明 ---
void testAblationStudy(std::shared_ptr<INIReader> config, const vector<util::Record> &records);

int main(int argc, char *argv[]) {

  auto config = util::read_ini("../config.ini");
  string data_file = config->Get("COMMON", "data_file", "");
  auto records = util::readTrace(data_file);
  std::set<FlowKey<13>> s;
  std::transform(records.begin(), records.end(), std::inserter(s, s.begin()),
                 [](auto &record) { return record.flowkey_; });
  printf("flow num is %ld \n", s.size());
  
//   testLFFilter(config, records);
//   testFDFilter(config, records);
  
  // --- [新增] 调用消融实验 ---
  testAblationStudy(config, records);

  return 0;
}

// 在 main.cc 中添加此函数，测试LF-Filter结构是否生效
void testLFFilter(std::shared_ptr<INIReader> config, const vector<util::Record> &records) {
    std::cout << "\n>>> Testing LF-Filter <<<" << std::endl;
    
    // 从配置读取阈值
    uint64_t delay_thres = config->GetInteger("COMMON", "delay_thres", 600000);
    uint64_t jitter_thres = config->GetInteger("COMMON", "jitter_thres", 100000);
    
    // 参数配置 (参考论文与内存限制)
    int n_hot = 3;
    int n_warm = 5;
    int n_cold = 3;
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


// =============================================================
//               以下为新增的消融实验完整实现代码
//           (请粘贴到 main.cc 文件的最末尾)
// =============================================================
void testAblationStudy(std::shared_ptr<INIReader> config, const vector<util::Record> &records) {
    std::cout << "\n*********************************************" << std::endl;
    std::cout << ">>> LF-Filter Ablation Study (Accuracy & Speed) <<<" << std::endl;
    std::cout << "*********************************************" << std::endl;

    uint64_t delay_thres = config->GetInteger("COMMON", "delay_thres", 600000);
    uint64_t jitter_thres = config->GetInteger("COMMON", "jitter_thres", 100000);
    
    // --- 统一参数配置 (目标: 控制总内存约 3MB 以保证公平) ---
    // FD-Filter 参数
    int fd_nbits = config->GetInteger("FDFilter", "nbits", 0);
    int fd_k = config->GetInteger("FDFilter", "k", 0);
    int fd_kk = config->GetInteger("FDFilter", "kk", 0);
    int fd_num_hash = config->GetInteger("FDFilter", "num_hash", 0); 
    int fd_gnbits = config->GetInteger("FDFilter", "gnbits", 0);
    int fd_gnum_hash = config->GetInteger("FDFilter", "gnum_hash", 0);
    int block_size = 4;
    fd_nbits = fd_nbits * (2*block_size) / (2*block_size+1);
    
    // LF-Filter 参数
    int lf_n_hot = 3;
    int lf_n_warm = 6;
    int lf_n_cold = 3;
    int lf_k_w = 3; 
    int lf_heavy_thres = 2;
    int lf_slots = 300000; 

    // ------------------------------------------------------------
    // Stage 1: Baseline (FD-Filter)
    // ------------------------------------------------------------
    {
        std::cout << "\n[Stage 1: Baseline (FD-Filter)]" << std::endl;
        
        // 1. 测准确性
        sketch::FDFilter<hash::AwareHash> sketch(fd_k, fd_kk, fd_nbits, fd_num_hash, fd_gnbits, fd_gnum_hash, delay_thres);
        standardTest(sketch, records, delay_thres, jitter_thres);

        // 2. 测速度 (新建实例，纯净测试)
        sketch::FDFilter<hash::AwareHash> speed_sketch(fd_k, fd_kk, fd_nbits, fd_num_hash, fd_gnbits, fd_gnum_hash, delay_thres);
        auto start = std::chrono::high_resolution_clock::now();
        volatile uint64_t dummy = 0; // 防止编译器优化掉循环
        for (const auto &rec : records) {
            dummy = speed_sketch.update(rec.flowkey_, rec.timestamp_);
        }
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;
        double mpps = ((double)records.size() / 1e6) / elapsed.count();
        
        std::cout << ">>> Execution Time: " << std::fixed << std::setprecision(4) << elapsed.count() << " s" << std::endl;
        std::cout << ">>> Throughput    : " << std::fixed << std::setprecision(2) << mpps << " Mpps" << std::endl;
    }

    // ------------------------------------------------------------
    // Stage 2: LF-Core (Layered + FP, No FlowAware)
    // ------------------------------------------------------------
    {
        std::cout << "\n[Stage 2: LF-Core (Layered + FP Only)]" << std::endl;
        
        // 1. 测准确性
        sketch::LFAblation<hash::AwareHash> sketch(lf_n_hot, lf_n_warm, lf_n_cold, lf_k_w, lf_slots, lf_heavy_thres, delay_thres, sketch::STAGE_2_CORE);
        standardTest(sketch, records, delay_thres, jitter_thres);

        // 2. 测速度
        sketch::LFAblation<hash::AwareHash> speed_sketch(lf_n_hot, lf_n_warm, lf_n_cold, lf_k_w, lf_slots, lf_heavy_thres, delay_thres, sketch::STAGE_2_CORE);
        auto start = std::chrono::high_resolution_clock::now();
        volatile uint64_t dummy = 0;
        for (const auto &rec : records) {
            dummy = speed_sketch.update(rec.flowkey_, rec.timestamp_);
        }
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;
        double mpps = ((double)records.size() / 1e6) / elapsed.count();
        
        std::cout << ">>> Execution Time: " << std::fixed << std::setprecision(4) << elapsed.count() << " s" << std::endl;
        std::cout << ">>> Throughput    : " << std::fixed << std::setprecision(2) << mpps << " Mpps" << std::endl;
    }

    // ------------------------------------------------------------
    // Stage 3: LF-FlowAware (Sequential Search - High Precision, Slow Speed)
    // ------------------------------------------------------------
    {
        std::cout << "\n[Stage 3: LF-FlowAware (Sequential Search)]" << std::endl;
        std::cout << "Note: Expecting Highest Accuracy but Lower Throughput than Stage 4" << std::endl;

        // 1. 测准确性
        sketch::LFAblation<hash::AwareHash> sketch(lf_n_hot, lf_n_warm, lf_n_cold, lf_k_w, lf_slots, lf_heavy_thres, delay_thres, sketch::STAGE_3_FLOW_AWARE);
        standardTest(sketch, records, delay_thres, jitter_thres);

        // 2. 测速度
        sketch::LFAblation<hash::AwareHash> speed_sketch(lf_n_hot, lf_n_warm, lf_n_cold, lf_k_w, lf_slots, lf_heavy_thres, delay_thres, sketch::STAGE_3_FLOW_AWARE);
        auto start = std::chrono::high_resolution_clock::now();
        volatile uint64_t dummy = 0;
        for (const auto &rec : records) {
            dummy = speed_sketch.update(rec.flowkey_, rec.timestamp_);
        }
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;
        double mpps = ((double)records.size() / 1e6) / elapsed.count();
        
        std::cout << ">>> Execution Time: " << std::fixed << std::setprecision(4) << elapsed.count() << " s" << std::endl;
        std::cout << ">>> Throughput    : " << std::fixed << std::setprecision(2) << mpps << " Mpps" << std::endl;
    }

    // ------------------------------------------------------------
    // Stage 4: Full LF-Filter (With Predictor - High Speed)
    // ------------------------------------------------------------
    {
        std::cout << "\n[Stage 4: Full LF-Filter (Predictor Enabled)]" << std::endl;
        std::cout << "Note: Expecting Highest Throughput (Time Optimization)" << std::endl;

        // 1. 测准确性
        // 使用 LFFilter 原版类，启用预测器
        sketch::LFFilter<hash::AwareHash> sketch(lf_n_hot, lf_n_warm, lf_n_cold, lf_k_w, lf_slots, 7, 2, delay_thres, lf_heavy_thres);
        standardTest(sketch, records, delay_thres, jitter_thres);

        // 【新增】打印预测器内部效率统计
        sketch.printPredictorStats();

        // 2. 测速度
        sketch::LFFilter<hash::AwareHash> speed_sketch(lf_n_hot, lf_n_warm, lf_n_cold, lf_k_w, lf_slots, 7, 2, delay_thres, lf_heavy_thres);
        auto start = std::chrono::high_resolution_clock::now();
        volatile uint64_t dummy = 0;
        for (const auto &rec : records) {
            dummy = speed_sketch.update(rec.flowkey_, rec.timestamp_);
        }
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;
        double mpps = ((double)records.size() / 1e6) / elapsed.count();
        
        std::cout << ">>> Execution Time: " << std::fixed << std::setprecision(4) << elapsed.count() << " s" << std::endl;
        std::cout << ">>> Throughput    : " << std::fixed << std::setprecision(2) << mpps << " Mops" << std::endl;
    }
}