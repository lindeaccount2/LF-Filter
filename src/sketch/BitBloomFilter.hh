#ifndef SKETCH_BITBF_HH
#define SKETCH_BITBF_HH

#include "common/flowkey.hh"
#include "common/hash.hh"
#include "sketch/BloomFilter.hh"
#include <algorithm>
#include <limits>
#include <string>

namespace sketch {

template <typename hash_t> class BitBf {
private:
  vector<BloomFilter<hash_t>> bfs_;
  int k_; // k_ bloom filters, representing k_ bits
  uint64_t start_time_;
  uint64_t delay_thres_;
  uint64_t last_update_;

public:
  BitBf(int k, int nbits, int num_hash, uint64_t delay_thres);
  BitBf(const BitBf &bf);
  BitBf(const BitBf &&bf) noexcept;

  ~BitBf();

  void setInitTime(uint64_t timestamp) {
    last_update_ = start_time_ = timestamp;
  };

  std::string name() { return "BitBf"; };

  size_t size() const;

  template <int32_t key_len>
  void update(const FlowKey<key_len> &flowkey, int window_num);

  template <int32_t key_len>
  uint64_t query(const FlowKey<key_len> &flowkey);

  auto clear() -> void;

  auto swap(BitBf<hash_t> &bf) -> void;

  void And(const BitBf<hash_t> &rhs);
  void Or(const BitBf<hash_t> &rhs);
};

template <typename hash_t>
BitBf<hash_t>::BitBf(int k, int nbits, int num_hash, uint64_t delay_thres)
    : k_(k), delay_thres_(delay_thres)
    , bfs_(k, BloomFilter<hash_t>(nbits, num_hash))
{
  // std::cout << "In bitbf: " << k_ << " " << nbits << std::endl;
  // bfs_.push_back(BloomFilter<hash_t>(nbits / 2, num_hash));
  // bfs_.push_back(BloomFilter<hash_t>(nbits / 2 * 3, num_hash));
}

template <typename hash_t>
BitBf<hash_t>::BitBf(const BitBf &bf) {
  k_ = bf.k_;
  for (int i = 0; i < k_; ++i) {
    bfs_.push_back(bf.bfs_[i]);
  }
}

template <typename hash_t>
BitBf<hash_t>::BitBf(const BitBf &&bf) noexcept {
  // cout << "move construct\n";
  k_ = bf.k_;
  for (int i = 0; i < k_; ++i) {
    bfs_.push_back(bf.bfs_[i]);
  }
}

template <typename hash_t> BitBf<hash_t>::~BitBf() {}

template <typename hash_t>
template <int32_t key_len>
void BitBf<hash_t>::update(const FlowKey<key_len> &flowkey,
                                     int window_num) {
    // uint32_t window_num = (timestamp - last_update_) / delay_thres_ + 1;
    // cout << "In Bitbf update: " << (int)window_num << " " << k_ << endl;
    assert(window_num <= (1 << k_) - 1);
    int i = 0;
    while (window_num) {
      if (window_num & 1)
          bfs_[i].insert(flowkey);
      window_num = window_num >> 1;
      i++;
    }
}

template <typename hash_t>
template <int32_t key_len>
uint64_t BitBf<hash_t>::query(const FlowKey<key_len> &flowkey) {
  uint64_t result = 0;
  for (int i = 0; i < k_; ++i) {
    if (bfs_[i].query(flowkey)) {
      result += (1 << i);
    }
  }
  return result;
}

template <typename hash_t> size_t BitBf<hash_t>::size() const {
  // std::cout << "BitBf size: " << bfs_[0].size() * (k_) << std::endl;
  // return bfs_[0].size() * (k_);
  size_t result = 0;
  for (auto &bf : bfs_) {
    result += bf.size();
  }
  return result;
}

template <typename hash_t> 
auto BitBf<hash_t>::clear() -> void {
  for(auto &bf: bfs_){
    bf.clear();
  }
}

template <typename hash_t> 
auto BitBf<hash_t>::swap(BitBf<hash_t> &bf) -> void {
  for (int i = 0; i < k_; ++i) {
    bfs_[i].swap(bf.bfs_[i]);
  }
}

template <typename hash_t>
void BitBf<hash_t>::And(const BitBf<hash_t> &rhs) {
  for (int i = 0; i < k_; ++i) {
    bfs_[i].And(rhs.bfs_[i]);
  }
}

template <typename hash_t>
void BitBf<hash_t>::Or(const BitBf<hash_t> &rhs) {
  for (int i = 0; i < k_; ++i) {
    bfs_[i].Or(rhs.bfs_[i]);
  }
}

} // namespace sketch

#endif