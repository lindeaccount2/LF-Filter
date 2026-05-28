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

template<typename hash_t>
class FlowClassifier {
    std::vector<uint8_t> counters_;
    int size_, hash_cnt_;
    hash_t hash_fn_;
public:
    FlowClassifier(int size, int hash_cnt) : size_(size), hash_cnt_(hash_cnt), counters_(size, 0) {}
    size_t size() const { return counters_.size() * sizeof(uint8_t); }

    template<int32_t key_len>
    bool isHeavy(const FlowKey<key_len>& key, int threshold) {
        uint8_t min_val = 255;
        std::vector<size_t> indices;
        indices.reserve(hash_cnt_);
        size_t h = hash_fn_(key);
        for(int i=0; i<hash_cnt_; ++i) {
            size_t idx = (h + i * 0x9e3779b9) % size_;
            indices.push_back(idx);
            if (counters_[idx] < min_val) min_val = counters_[idx];
        }
        if (min_val < 255) {
            for (size_t idx : indices) { if (counters_[idx] == min_val) counters_[idx]++; }
        }
        return (min_val + 1) >= threshold;
    }
};

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
        uint8_t target = (observed_layer == 1) ? 120 : ((observed_layer == 2) ? 240 : 0);
        uint8_t current = table_[idx];
        table_[idx] = current - (current >> 2) + (target >> 2);
    }
};

template <typename hash_t>
class LFFilter {
private:
    BloomFilter<hash_t> existence_filter_;
    FlowClassifier<hash_t> classifier_;
    Predictor<hash_t> predictor_;

    using FilterBlock = std::vector<uint64_t>;
    std::deque<FilterBlock> hot_layer_, warm_layer_, cold_layer_;
    FilterBlock hot_eviction_buffer_, warm_eviction_buffer_;
    bool hot_buffer_full_ = false, warm_buffer_full_ = false;

    int n_hot_, n_warm_, n_cold_;
    uint64_t T_l_;
    int k_w_, slots_per_filter_, heavy_threshold_;
    static const int PROBE_LIMIT = 4;
    uint64_t last_update_ = 0;
    bool initialized_ = false;
    hash_t hash_fn_;
    std::mt19937 rng_;

    long long total_heavy_queries_ = 0;
    long long pred_hits_ = 0;
    long long skipped_lookups_ = 0;
    long long cost_baseline_ = 0;
    long long cost_predictor_ = 0;

public:
    LFFilter(int n_h, int n_w, int n_c, int k_w, int slots, int, int, uint64_t delay_thres, int heavy_thres)
        : n_hot_(n_h), n_warm_(n_w), n_cold_(n_c), k_w_(k_w), slots_per_filter_(slots), heavy_threshold_(heavy_thres),
          existence_filter_(slots * 8, 5), classifier_(slots, 3), predictor_(slots), rng_(12345)
    {
        if (n_hot_ > 0) T_l_ = delay_thres / (3 * n_hot_); else T_l_ = delay_thres;
        if (T_l_ == 0) T_l_ = 1;
        int u64 = (slots + 7) / 8; int c_u64 = (slots * 2 + 63) / 64;
        for(int i=0; i<n_hot_; ++i) hot_layer_.push_back(FilterBlock(u64, 0));
        for(int i=0; i<n_warm_; ++i) warm_layer_.push_back(FilterBlock(u64, 0));
        for(int i=0; i<n_cold_; ++i) cold_layer_.push_back(FilterBlock(c_u64, 0));
        hot_eviction_buffer_.resize(u64, 0); warm_eviction_buffer_.resize(u64, 0);
    }

    std::string name() { return "LFFilter"; }
    void setInitTime(uint64_t timestamp) { last_update_ = timestamp; initialized_ = true; }

    size_t size() const {
        size_t h = (n_hot_ > 0 && !hot_layer_.empty()) ? n_hot_ * hot_layer_[0].size() * 8 : 0;
        size_t w = (n_warm_ > 0 && !warm_layer_.empty()) ? n_warm_ * warm_layer_[0].size() * 8 : 0;
        size_t c = (n_cold_ > 0 && !cold_layer_.empty()) ? n_cold_ * cold_layer_[0].size() * 8 : 0;
        return (existence_filter_.size()/8) + classifier_.size() + predictor_.size() + h + w + c;
    }

    void printMemoryBreakdown() {
        std::cout << "Size: " << size() / 1024.0 << " KB" << std::endl;
    }

    void printPredictorStats(double unused = 0.0) {
        std::cout << "\n=============================================" << std::endl;
        std::cout << ">>> Predictor Efficiency Analysis <<<" << std::endl;
        std::cout << "=============================================" << std::endl;

        if (total_heavy_queries_ == 0 || cost_baseline_ == 0) {
            std::cout << "No heavy flow queries recorded." << std::endl;
            return;
        }

        double hit_rate = (double)pred_hits_ / total_heavy_queries_ * 100.0;

        double time_opt_pct = 0.0;
        if (cost_baseline_ > 0) {
            time_opt_pct = (1.0 - (double)cost_predictor_ / (double)cost_baseline_) * 100.0;
        }

        std::cout << "Total Heavy Queries  : " << total_heavy_queries_ << std::endl;
        std::cout << "Predictor Hit Rate   : " << std::fixed << std::setprecision(2) << hit_rate << "%" << std::endl;
        std::cout << "---------------------------------------------" << std::endl;
        std::cout << "(1) Reduced Query Count (Skipped Lookups): " << skipped_lookups_ << std::endl;
        std::cout << "    - Baseline Cost (Total Accesses)     : " << cost_baseline_ << std::endl;
        std::cout << "    - Predictor Cost (Total Accesses)    : " << cost_predictor_ << std::endl;
        std::cout << "---------------------------------------------" << std::endl;
        std::cout << "(2) Time Optimization Percentage         : " << std::fixed << std::setprecision(4) << time_opt_pct << "%" << std::endl;
        std::cout << "=============================================\n" << std::endl;
    }

    template <int32_t key_len>
    uint64_t update(const FlowKey<key_len> &flowkey, uint64_t timestamp) {
        if (!initialized_) setInitTime(timestamp);
        while (timestamp > last_update_ && timestamp - last_update_ >= T_l_) {
            rotate_layers(); last_update_ += T_l_;
        }

        bool is_new = !existence_filter_.query(flowkey);
        if (is_new) existence_filter_.insert(flowkey);

        size_t h = hash_fn_(flowkey);

        uint64_t delay = is_new ? 0 : query_internal(flowkey, timestamp, h);
        write_to_layer(flowkey, h);
        return delay;
    }

private:
    template <int32_t key_len>
    uint64_t query_internal(const FlowKey<key_len> &flowkey, uint64_t timestamp, size_t h) {
        if (!classifier_.isHeavy(flowkey, heavy_threshold_)) return 0;
        total_heavy_queries_++;

        int predicted = predictor_.predict(flowkey);
        int found = -1;
        uint64_t res = 0;
        uint64_t wb = n_hot_ * T_l_, cb = wb + n_warm_ * 2 * T_l_;

        auto search = [&](std::deque<FilterBlock>& l, uint64_t w, uint64_t b, int t) {
            return search_layer(l, flowkey, w, true, b, t, timestamp, last_update_, h);
        };

        auto check = [&](int layer) -> bool {
            if (layer == 0) { if ((res = search(hot_layer_, T_l_, 0, 0))) { found = 0; return true; } }
            else if (layer == 1) { if ((res = search(warm_layer_, 2*T_l_, wb, 1))) { found = 1; return true; } }
            else { if ((res = search(cold_layer_, 4*T_l_, cb, 2))) { found = 2; return true; } }
            return false;
        };

        if (predicted == 0) { check(0); if(found<0) check(1); if(found<0) check(2); }
        else if (predicted == 1) { check(1); if(found<0) check(0); if(found<0) check(2); }
        else { check(2); if(found<0) check(1); if(found<0) check(0); }

        if (found != -1) {
            predictor_.update(flowkey, found);

            if (predicted == found) {
                pred_hits_++;
                if (found == 1) skipped_lookups_ += 1;
                else if (found == 2) skipped_lookups_ += 2;
            }

            cost_baseline_ += (found + 1);

            static const int cost_matrix[3][3] = {
                {1, 2, 3},
                {2, 1, 3},
                {3, 2, 1}
            };
            cost_predictor_ += cost_matrix[predicted][found];
        }
        return res;
    }

    template <int32_t key_len>
    void write_to_layer(const FlowKey<key_len> &flowkey, size_t h) {
        bool is_heavy = classifier_.isHeavy(flowkey, heavy_threshold_);
        uint64_t fp = (h & 0x7F); if (fp==0) fp=1;
        update_slot(hot_layer_.front(), fp, is_heavy, flowkey, h);
    }

    template <int32_t key_len>
    void update_slot(FilterBlock& filter, uint64_t fp, bool is_heavy, const FlowKey<key_len>& key, size_t h) {
        for (int i=0; i<k_w_; ++i) {
            size_t base = (h + i*0x9e3779b9) % slots_per_filter_;
            bool ok = false;
            for(int p=0; p<PROBE_LIMIT; ++p) {
                size_t idx = (base+p)%slots_per_filter_;
                uint64_t mask = 0xFFULL << ((idx%8)*8);
                uint64_t val = (filter[idx/8] & mask) >> ((idx%8)*8);
                if ((val&1)==0 || (val>>1)==fp) {
                    filter[idx/8] &= ~mask;
                    filter[idx/8] |= (((fp<<1)|1) << ((idx%8)*8));
                    ok = true; break;
                }
            }
            if (!ok) {
                if (is_heavy || (rng_()%100 < 5)) {
                    size_t idx = base;
                    filter[idx/8] &= ~(0xFFULL << ((idx%8)*8));
                    filter[idx/8] |= (((fp<<1)|1) << ((idx%8)*8));
                }
            }
        }
    }

    template <int32_t key_len>
    uint64_t search_layer(std::deque<FilterBlock>& layer, const FlowKey<key_len>& key,
                          uint64_t win, bool is_heavy, uint64_t base, int type, uint64_t ts, uint64_t last, size_t h) {
        uint64_t my_fp = (h & 0x7F); if(my_fp==0) my_fp=1;
        for(size_t f=0; f<layer.size(); ++f) {
            int cnt = 0;
            for(int i=0; i<k_w_; ++i) {
                size_t base_idx = (h + i*0x9e3779b9) % slots_per_filter_;
                for(int p=0; p<PROBE_LIMIT; ++p) {
                    size_t idx = (base_idx+p)%slots_per_filter_;
                    if (type == 2) {
                        if (p>0) break;
                        uint64_t val = (layer[f][idx/32] >> ((idx%32)*2)) & 0x3;
                        if ((is_heavy && (val&2)) || (!is_heavy && (val&1))) { cnt++; break; }
                    } else {
                        uint64_t val = (layer[f][idx/8] >> ((idx%8)*8)) & 0xFF;
                        if ((val&1) && (val>>1)==my_fp) { cnt++; break; }
                    }
                }
            }
            if (cnt == k_w_) {
                if (type==0 && f==0) return (ts-last==0)?1:(ts-last)/2;
                return base + f*win + win/3;
            }
        }
        return 0;
    }

    void merge_blocks(FilterBlock& d, const FilterBlock& s) {
        for(size_t i=0; i<d.size(); ++i) {
            uint64_t dv=d[i], sv=s[i], res=0;
            for(int b=0; b<8; ++b) {
                uint64_t ds=(dv>>(b*8))&0xFF, ss=(sv>>(b*8))&0xFF;
                res |= (((ds&1)?ds:ss) << (b*8));
            }
            d[i] = res;
        }
    }
    void rotate_layers() {
        FilterBlock b = hot_layer_.back(); hot_layer_.pop_back();
        hot_layer_.push_front(FilterBlock(b.size(),0));
        merge_blocks(hot_eviction_buffer_, b);
        if(!hot_buffer_full_) hot_buffer_full_=true; else { rotate_warm(hot_eviction_buffer_); std::fill(hot_eviction_buffer_.begin(),hot_eviction_buffer_.end(),0); hot_buffer_full_=false; }
    }
    void rotate_warm(const FilterBlock& in) {
        FilterBlock b = warm_layer_.back(); warm_layer_.pop_back();
        warm_layer_.push_front(in);
        merge_blocks(warm_eviction_buffer_, b);
        if(!warm_buffer_full_) warm_buffer_full_=true; else { rotate_cold(warm_eviction_buffer_); std::fill(warm_eviction_buffer_.begin(),warm_eviction_buffer_.end(),0); warm_buffer_full_=false; }
    }
    void rotate_cold(const FilterBlock& in) {
        cold_layer_.pop_back();
        FilterBlock c(cold_layer_.front().size(), 0);
        for(int i=0; i<slots_per_filter_; ++i) {
            uint64_t val = (in[i/8] >> ((i%8)*8)) & 0xFF;
            if (val&1) {
                uint64_t cv = (val>>1)>0 ? 2 : 1;
                c[i/32] |= (cv << ((i%32)*2));
            }
        }
        cold_layer_.push_front(c);
    }
};
}
#endif
