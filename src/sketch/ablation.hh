#ifndef SKETCH_LFFILTER_HH
#define SKETCH_LFFILTER_HH

#include "common/flowkey.hh"
#include "common/hash.hh"
#include "sketch/BloomFilter.hh"
#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <iomanip>

namespace sketch {

// --- 模块1: Flow Classifier ---
template<typename hash_t>
class FlowClassifier {
    std::vector<uint8_t> counters_;
    int size_;
    int hash_cnt_;
    hash_t hash_fn_;
public:
    FlowClassifier(int size, int hash_cnt) : size_(size), hash_cnt_(hash_cnt), counters_(size, 0) {}
    size_t size() const { return counters_.size() * sizeof(uint8_t); }
    
    template<int32_t key_len>
    bool isHeavy(const FlowKey<key_len>& key, int threshold) {
        uint8_t min_val = 255;
        std::vector<size_t> indices;
        for(int i=0; i<hash_cnt_; ++i) {
            size_t idx = (hash_fn_(key) + i * 0x9e3779b9) % size_;
            indices.push_back(idx);
            if (counters_[idx] < min_val) min_val = counters_[idx];
        }
        if (min_val < 255) {
            for (size_t idx : indices) {
                if (counters_[idx] == min_val) counters_[idx]++;
            }
        }
        return (min_val + 1) >= threshold;
    }
};

// --- 模块2: Predictor ---
template<typename hash_t>
class Predictor {
    std::vector<uint8_t> table_; 
    int size_;
    hash_t hash_fn_;
public:
    Predictor(int size) : size_(size), table_(size, 0) {} 
    size_t size() const { return table_.size() * sizeof(uint8_t); }
    
    template<int32_t key_len>
    int predict(const FlowKey<key_len>& key) {
        size_t idx = hash_fn_(key) % size_;
        uint8_t val = table_[idx];
        if (val < 60) return 0;
        if (val < 180) return 1;
        return 2;
    }

    template<int32_t key_len>
    void update(const FlowKey<key_len>& key, int observed_layer) {
        size_t idx = hash_fn_(key) % size_;
        uint8_t target = 0;
        if (observed_layer == 1) target = 120;
        else if (observed_layer == 2) target = 240;
        uint8_t current = table_[idx];
        table_[idx] = current - (current >> 2) + (target >> 2);
    }
};

// --- 模块3: LF-Filter 主类 ---
template <typename hash_t>
class LFFilter {
private:
    BloomFilter<hash_t> existence_filter_;
    FlowClassifier<hash_t> classifier_;
    Predictor<hash_t> predictor_;

    using FilterBlock = std::vector<uint64_t>;
    std::deque<FilterBlock> hot_layer_; 
    std::deque<FilterBlock> warm_layer_;
    std::deque<FilterBlock> cold_layer_;

    FilterBlock hot_eviction_buffer_;
    FilterBlock warm_eviction_buffer_;
    bool hot_buffer_full_ = false;
    bool warm_buffer_full_ = false;

    int n_hot_, n_warm_, n_cold_;
    uint64_t T_l_; 
    int k_w_;      
    int slots_per_filter_; 
    int heavy_threshold_;

    static const int PROBE_LIMIT = 4; 

    uint64_t last_update_ = 0;
    bool initialized_ = false; 
    hash_t hash_fn_;
    std::mt19937 rng_;

    // --- 统计变量 ---
    long long total_heavy_queries_ = 0;
    long long pred_hits_ = 0;
    long long skipped_lookups_ = 0; // 指标(1): 减少的查询次数
    
    // 开销模型变量 (用于指标2)
    long long cost_baseline_ = 0;   // Stage 3 开销 (Hot->Warm->Cold)
    long long cost_predictor_ = 0;  // Stage 4 开销 (Actual Path)

public:
    LFFilter(int n_h, int n_w, int n_c, int k_w, 
             int slots, int /*d1*/, int /*d2*/, 
             uint64_t delay_thres, int heavy_thres)
        : n_hot_(n_h), n_warm_(n_w), n_cold_(n_c), k_w_(k_w), 
          slots_per_filter_(slots), 
          heavy_threshold_(heavy_thres),
          existence_filter_(slots * 8, 5), 
          classifier_(slots, 3), 
          predictor_(slots),
          rng_(12345)
    {
        if (n_hot_ > 0) T_l_ = delay_thres / (3 * n_hot_);
        else T_l_ = delay_thres;
        if (T_l_ == 0) T_l_ = 1;

        int uint64_cnt = (slots_per_filter_ + 7) / 8;
        int cold_uint64_cnt = (slots_per_filter_ * 2 + 63) / 64;

        for(int i=0; i<n_hot_; ++i) hot_layer_.push_back(FilterBlock(uint64_cnt, 0));
        for(int i=0; i<n_warm_; ++i) warm_layer_.push_back(FilterBlock(uint64_cnt, 0));
        for(int i=0; i<n_cold_; ++i) cold_layer_.push_back(FilterBlock(cold_uint64_cnt, 0));

        hot_eviction_buffer_.resize(uint64_cnt, 0);
        warm_eviction_buffer_.resize(uint64_cnt, 0);
    }

    std::string name() { return "LFFilter"; }
    void setInitTime(uint64_t timestamp) { last_update_ = timestamp; initialized_ = true; }

    // 【修复报错】补全 size() 函数
    size_t size() const {
        size_t ef = existence_filter_.size() / 8; 
        size_t cms = classifier_.size(); 
        size_t pred = predictor_.size(); 
        size_t hw_layers = (n_hot_ + n_warm_) * hot_layer_[0].size() * 8;
        size_t c_layers = n_cold_ * cold_layer_[0].size() * 8;
        return ef + cms + pred + hw_layers + c_layers; 
    }

    // 【修复报错】补全 printMemoryBreakdown() 函数
    void printMemoryBreakdown() {
        double to_kb = 1024.0;
        size_t total = size();
        std::cout << "\n=== LF-Filter Memory Breakdown ===" << std::endl;
        std::cout << "Slots: " << slots_per_filter_ << std::endl;
        std::cout << "TOTAL: " << total / to_kb << " KB (" << total / (1024.0*1024.0) << " MB)" << std::endl;
        std::cout << "==================================\n" << std::endl;
    }

    // --- 【新增】打印两个关键指标 ---
    void printPredictorStats() {
        std::cout << "\n=============================================" << std::endl;
        std::cout << ">>> Predictor Efficiency Analysis <<<" << std::endl;
        std::cout << "=============================================" << std::endl;
        
        if (total_heavy_queries_ == 0) {
            std::cout << "No heavy flow queries recorded." << std::endl;
            return;
        }

        double hit_rate = (double)pred_hits_ / total_heavy_queries_ * 100.0;
        
        // 计算指标(2): 时间优化百分比
        double optimization_pct = 0.0;
        if (cost_baseline_ > 0) {
            optimization_pct = (1.0 - (double)cost_predictor_ / (double)cost_baseline_) * 100.0;
        }

        std::cout << "Total Heavy Queries  : " << total_heavy_queries_ << std::endl;
        std::cout << "Predictor Hit Rate   : " << std::fixed << std::setprecision(2) << hit_rate << "%" << std::endl;
        std::cout << "---------------------------------------------" << std::endl;
        // 指标 1
        std::cout << "(1) Reduced Query Count (Skipped Lookups): " << skipped_lookups_ << std::endl;
        
        std::cout << "    - Baseline Cost (Total Accesses)     : " << cost_baseline_ << std::endl;
        std::cout << "    - Predictor Cost (Total Accesses)    : " << cost_predictor_ << std::endl;
        std::cout << "---------------------------------------------" << std::endl;
        // 指标 2
        std::cout << "(2) Time Optimization Percentage         : " << std::fixed << std::setprecision(4) << optimization_pct << "%" << std::endl;
        std::cout << "=============================================\n" << std::endl;
    }

    template <int32_t key_len>
    uint64_t update(const FlowKey<key_len> &flowkey, uint64_t timestamp) {
        if (!initialized_) setInitTime(timestamp);

        while (timestamp > last_update_ && timestamp - last_update_ >= T_l_) {
            rotate_layers();
            last_update_ += T_l_;
        }
        
        bool is_new_flow = false;
        if (!existence_filter_.query(flowkey)) {
            existence_filter_.insert(flowkey);
            is_new_flow = true; 
        }

        uint64_t detected_delay = 0;
        if (!is_new_flow) {
            detected_delay = query_internal(flowkey, timestamp);
        }

        write_to_layer(flowkey); 
        return detected_delay;
    }

private:
    template <int32_t key_len>
    uint64_t query_internal(const FlowKey<key_len> &flowkey, uint64_t timestamp) {
        // Light Flow 不计入统计，直接返回
        if (!classifier_.isHeavy(flowkey, heavy_threshold_)) { return 0; }

        total_heavy_queries_++;
        uint64_t result = 0;
        uint64_t warm_base = n_hot_ * T_l_;
        uint64_t cold_base = warm_base + n_warm_ * 2 * T_l_;

        int predicted_layer = predictor_.predict(flowkey);
        int found_layer = -1;

        auto search = [&](std::deque<FilterBlock>& layer, uint64_t win, uint64_t base, int l_type) {
            return search_layer(layer, flowkey, win, true, base, l_type, timestamp, last_update_);
        };

        // 执行查询 (Stage 4 预测逻辑)
        if (predicted_layer == 0) { 
            if ((result = search(hot_layer_, T_l_, 0, 0))) found_layer = 0;
            else if ((result = search(warm_layer_, 2*T_l_, warm_base, 1))) found_layer = 1;
            else if ((result = search(cold_layer_, 4*T_l_, cold_base, 2))) found_layer = 2;
        } else if (predicted_layer == 1) { 
            if ((result = search(warm_layer_, 2*T_l_, warm_base, 1))) found_layer = 1;
            else if ((result = search(hot_layer_, T_l_, 0, 0))) found_layer = 0;
            else if ((result = search(cold_layer_, 4*T_l_, cold_base, 2))) found_layer = 2;
        } else { 
            if ((result = search(cold_layer_, 4*T_l_, cold_base, 2))) found_layer = 2;
            else if ((result = search(warm_layer_, 2*T_l_, warm_base, 1))) found_layer = 1;
            else if ((result = search(hot_layer_, T_l_, 0, 0))) found_layer = 0;
        }

        if (found_layer != -1) {
            predictor_.update(flowkey, found_layer);
            if (predicted_layer == found_layer) pred_hits_++;

            // --- 核心计算逻辑 ---

            // 1. 计算指标 (1): Skipped Lookups (减少的查询次数)
            // 只有当预测准确，且位于Warm或Cold层时，才真正发生了跳跃
            if (predicted_layer == found_layer) {
                if (found_layer == 1) skipped_lookups_ += 1; // 跳过Hot
                else if (found_layer == 2) skipped_lookups_ += 2; // 跳过Hot+Warm
            }

            // 2. 计算指标 (2): Cost Model (时间优化百分比)
            
            // Baseline Cost (Stage 3): 总是固定顺序 Hot(1) -> Warm(2) -> Cold(3)
            // Found in Hot(0): cost=1
            // Found in Warm(1): cost=2
            // Found in Cold(2): cost=3
            cost_baseline_ += (found_layer + 1);

            // Predictor Cost (Stage 4): 实际执行的查找次数
            int current_cost = 0;
            if (predicted_layer == 0) {
                // 顺序: Hot -> Warm -> Cold
                if (found_layer == 0) current_cost = 1;
                else if (found_layer == 1) current_cost = 2;
                else if (found_layer == 2) current_cost = 3;
            } 
            else if (predicted_layer == 1) {
                // 顺序: Warm -> Hot -> Cold
                if (found_layer == 1) current_cost = 1;      // 命中Warm，代价1
                else if (found_layer == 0) current_cost = 2; // 没命中Warm，查Hot，代价2
                else if (found_layer == 2) current_cost = 3;
            } 
            else if (predicted_layer == 2) {
                // 顺序: Cold -> Warm -> Hot
                if (found_layer == 2) current_cost = 1;      // 命中Cold，代价1
                else if (found_layer == 1) current_cost = 2;
                else if (found_layer == 0) current_cost = 3;
            }
            cost_predictor_ += current_cost;
        }
        return result;
    }

    template <int32_t key_len>
    void write_to_layer(const FlowKey<key_len> &flowkey) {
        bool is_heavy = classifier_.isHeavy(flowkey, heavy_threshold_);
        uint64_t fp = (hash_fn_(flowkey) & 0x7F);
        if (fp == 0) fp = 1; 
        update_slot(hot_layer_.front(), fp, is_heavy, flowkey);
    }

    template <int32_t key_len>
    void update_slot(FilterBlock& filter, uint64_t fp, bool is_heavy, const FlowKey<key_len>& key) {
        for (int i = 0; i < k_w_; ++i) {
            size_t base_idx = (hash_fn_(key) + i*0x9e3779b9) % slots_per_filter_;
            bool written = false;

            for (int p = 0; p < PROBE_LIMIT; ++p) {
                size_t idx = (base_idx + p) % slots_per_filter_;
                size_t word_idx = idx / 8;
                size_t bit_offset = (idx % 8) * 8;
                uint64_t mask = 0xFFULL << bit_offset;
                uint64_t raw = (filter[word_idx] & mask) >> bit_offset;
                uint64_t exist_fp = raw >> 1; 
                uint64_t exist_win = raw & 0x1;

                if (exist_win == 0) { 
                    uint64_t new_val = (fp << 1) | 1;
                    filter[word_idx] &= ~mask;
                    filter[word_idx] |= (new_val << bit_offset);
                    written = true;
                    break;
                } else if (exist_fp == fp) { 
                    uint64_t new_val = (fp << 1) | 1;
                    filter[word_idx] &= ~mask;
                    filter[word_idx] |= (new_val << bit_offset);
                    written = true;
                    break;
                }
            }

            if (!written) {
                if (is_heavy) {
                    size_t idx = base_idx;
                    size_t word_idx = idx / 8;
                    size_t bit_offset = (idx % 8) * 8;
                    uint64_t mask = 0xFFULL << bit_offset;
                    uint64_t new_val = (fp << 1) | 1;
                    filter[word_idx] &= ~mask;
                    filter[word_idx] |= (new_val << bit_offset);
                } else {
                    if ((rng_() % 100) < 5) { 
                        size_t idx = base_idx; 
                        size_t word_idx = idx / 8;
                        size_t bit_offset = (idx % 8) * 8;
                        uint64_t mask = 0xFFULL << bit_offset;
                        uint64_t new_val = (fp << 1) | 1;
                        filter[word_idx] &= ~mask;
                        filter[word_idx] |= (new_val << bit_offset);
                    }
                }
            }
        }
    }

    template <int32_t key_len>
    uint64_t search_layer(std::deque<FilterBlock>& layer, const FlowKey<key_len>& key, 
                          uint64_t win_len, bool is_heavy, uint64_t base_delay_offset, 
                          int layer_type, uint64_t timestamp, uint64_t last_update) {
        
        uint64_t my_fp = (hash_fn_(key) & 0x7F);
        if (my_fp == 0) my_fp = 1;

        for (size_t f_idx = 0; f_idx < layer.size(); ++f_idx) {
            auto& filter = layer[f_idx];
            int match_count = 0;
            for (int i = 0; i < k_w_; ++i) {
                size_t base_idx = (hash_fn_(key) + i*0x9e3779b9) % slots_per_filter_;
                bool found_in_probe = false;
                for (int p = 0; p < PROBE_LIMIT; ++p) {
                    size_t idx = (base_idx + p) % slots_per_filter_;
                    
                    if (layer_type == 2) { // Cold
                        if (p > 0) break; 
                        size_t c_word_idx = idx / 32;
                        size_t c_bit_offset = (idx % 32) * 2;
                        uint64_t c_raw = (filter[c_word_idx] >> c_bit_offset) & 0x3;
                        if (is_heavy) { if ((c_raw & 0x2) > 0) found_in_probe = true; } 
                        else { if ((c_raw & 0x1) > 0) found_in_probe = true; }
                    } else { // Hot/Warm
                        size_t word_idx = idx / 8;
                        size_t bit_offset = (idx % 8) * 8;
                        uint64_t raw = (filter[word_idx] >> bit_offset) & 0xFF;
                        if ((raw & 0x1) > 0) { 
                            uint64_t exist_fp = raw >> 1;
                            if (exist_fp == my_fp) { found_in_probe = true; break; }
                        }
                    }
                }
                if (found_in_probe) match_count++;
            }

            if (match_count == k_w_) {
                if (layer_type == 0 && f_idx == 0) {
                     uint64_t elapsed = timestamp - last_update;
                     return (elapsed == 0) ? 1 : elapsed / 2;
                }
                return base_delay_offset + f_idx * win_len + (win_len / 3);
            }
        }
        return 0;
    }

    void rotate_layers() {
        FilterBlock hot_evicted = hot_layer_.back();
        hot_layer_.pop_back();
        FilterBlock new_block(hot_evicted.size(), 0);
        hot_layer_.push_front(new_block);
        merge_blocks(hot_eviction_buffer_, hot_evicted);
        if (!hot_buffer_full_) { hot_buffer_full_ = true; } 
        else {
            rotate_warm_layer(hot_eviction_buffer_);
            std::fill(hot_eviction_buffer_.begin(), hot_eviction_buffer_.end(), 0);
            hot_buffer_full_ = false;
        }
    }
    void merge_blocks(FilterBlock& dest, const FilterBlock& src) {
        for (size_t i = 0; i < dest.size(); ++i) {
            uint64_t dest_val = dest[i];
            uint64_t src_val = src[i];
            uint64_t res = 0;
            for(int b=0; b<8; ++b) {
                int shift = b*8;
                uint64_t d_slot = (dest_val >> shift) & 0xFF;
                uint64_t s_slot = (src_val >> shift) & 0xFF;
                uint64_t final_slot = d_slot;
                if ((d_slot & 1) == 0) final_slot = s_slot; 
                res |= (final_slot << shift);
            }
            dest[i] = res;
        }
    }
    void rotate_warm_layer(const FilterBlock& incoming_data) {
        FilterBlock warm_evicted = warm_layer_.back();
        warm_layer_.pop_back();
        warm_layer_.push_front(incoming_data);
        merge_blocks(warm_eviction_buffer_, warm_evicted);
        if (!warm_buffer_full_) { warm_buffer_full_ = true; } 
        else {
            rotate_cold_layer(warm_eviction_buffer_);
            std::fill(warm_eviction_buffer_.begin(), warm_eviction_buffer_.end(), 0);
            warm_buffer_full_ = false;
        }
    }
    void rotate_cold_layer(const FilterBlock& incoming_data) {
        cold_layer_.pop_back();
        int cold_size = cold_layer_.front().size();
        FilterBlock compressed_data(cold_size, 0);
        for (int slot_idx = 0; slot_idx < slots_per_filter_; ++slot_idx) {
            int in_word = slot_idx / 8;
            int in_bit = (slot_idx % 8) * 8;
            uint64_t val = (incoming_data[in_word] >> in_bit) & 0xFF;
            int out_word = slot_idx / 32;
            int out_bit = (slot_idx % 32) * 2;
            uint64_t fp = val >> 1;
            uint64_t win = val & 0x1;
            uint64_t c_val = 0;
            if (win) {
                if (fp > 0) c_val = 0x2; else c_val = 0x1;        
            }
            if (c_val > 0) compressed_data[out_word] |= (c_val << out_bit);
        }
        cold_layer_.push_front(compressed_data);
    }
};

} // namespace sketch
#endif