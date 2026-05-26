#ifndef SKETCH_MERGEABLEFDFILTER_HH
#define SKETCH_MERGEABLEFDFILTER_HH

#include "common/flowkey.hh"
#include "common/hash.hh"
#include "sketch/BitBloomFilter.hh"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <list>
#include <queue>
#include <string>
using std::deque;
using std::list;
using std::pair;
using std::vector;

namespace sketch {
template <typename hash_t>
class MergeableFDFilter {
private:
  BitBf<hash_t> empty_bf_;
  // vector<BitBf<hash_t>> bfs_;
  deque<BitBf<hash_t>> bfs_;
  BloomFilter<hash_t> gbf_;
  list<pair<BitBf<hash_t>, int>>
      old_bfs_;
  int k_;
  int kk_;
  int part;
  int sub_win_num = 0;
  uint64_t start_time_;
  uint64_t window_time_;
  uint64_t delay_thres_;
  uint64_t last_update_;
  std::chrono::nanoseconds thput_time_;
  uint64_t thput_num_;

public:
  MergeableFDFilter(int k, int kk, int nbits, int num_hash,
  	 			int gnbits, int gnum_hash, uint64_t delay_thres);
  ~MergeableFDFilter();
  void setInitTime(uint64_t timestamp) {
    last_update_ = start_time_ = timestamp;
  };
  std::string name() { return "MergeableFDFilter"; };
  size_t size() const;
  auto throughput() -> double {
    return (1.0 * thput_num_ / 1e6) / (1.0 * thput_time_.count() / 1e9);
  }
  template <int32_t key_len>
  uint64_t update(const FlowKey<key_len> &flowkey, uint64_t timestamp);
  auto clear() -> void;
  void mergeOldBfs(const BitBf<hash_t> &bf);
  template <int32_t key_len>
  uint64_t estimateTimeByOldBfs(const FlowKey<key_len> &flowkey);

};

template <typename hash_t>
MergeableFDFilter<hash_t>::MergeableFDFilter(int k, int kk, int nbits, int num_hash,
									int gnbits, int gnum_hash, uint64_t delay_thres)
    : k_(k), kk_(kk), delay_thres_(delay_thres), thput_num_(0),
      thput_time_(std::chrono::nanoseconds::zero()),
    	gbf_(gnbits, gnum_hash),
      empty_bf_(kk, nbits, num_hash, delay_thres / (k * ((1 << kk) - 1)))
    	// bfs_(k + 1, BitBf<hash_t>(kk, nbits, num_hash, delay_thres / (k * ((1 << kk) - 1)) ) )
{
	// std::cout << "In MergeableFDFilter:" << k << " " << kk << endl;
  bfs_.push_front(empty_bf_);
	part = k * ((1 << kk) - 1);
  window_time_ = delay_thres / part;
}

template <typename hash_t> MergeableFDFilter<hash_t>::~MergeableFDFilter() {}

template <typename hash_t>
template <int32_t key_len>
uint64_t MergeableFDFilter<hash_t>::update(const FlowKey<key_len> &flowkey, uint64_t timestamp) {
	// if the timestamp is outside of the latest window
	if ((timestamp - last_update_) * part >= delay_thres_) {
		// std::cout << timestamp << " " << last_update_ << std::endl;
		last_update_ = timestamp;
		sub_win_num++;
    if (sub_win_num % ((1 << kk_) - 1) == 0) { // no empty local filters
      bfs_.push_front(empty_bf_);
      if (bfs_.size() > (k_ + 1)) {
        mergeOldBfs(bfs_.back());
        // old_bfs_.push_front(std::make_pair(bfs_.back(), 1));
        bfs_.pop_back();
      }
    }
	}

  ++thput_num_;
	if (!gbf_.query(flowkey)) {
		gbf_.insert(flowkey);
		// bfs_[k_].update(flowkey, sub_win_num % ((1 << kk_) - 1) + 1);
    bfs_.front().update(flowkey, sub_win_num % ((1 << kk_) - 1) + 1);
		return 0;
	}

	int i = 0;
	uint64_t ret = 0;
  int bfs_size = bfs_.size();
  for (; i < bfs_size; ++i) {
    if ((ret = bfs_[i].query(flowkey)) != 0) {
      break;
    }
  }

	int now = sub_win_num % ((1 << kk_) - 1) + 1;

  int out = 0;
  bfs_[0].update(flowkey, now);
	// std::cout << "Ret: " << ret << " " << timestamp - last_update_ << " " << now << " " << (1 << kk_) - 1 << " " << i << std::endl;
  if (i == 0) {
    // cout << "found: " << timestamp - last_update_ << endl;
    if (ret == now)
			return timestamp - last_update_;
		else
			return timestamp - last_update_ + (now - 1) * window_time_;
	}
  
  auto start_time = std::chrono::steady_clock::now();
  auto finish_time = std::chrono::steady_clock::now();
  thput_time_ += std::chrono::duration_cast<std::chrono::nanoseconds>(
      finish_time - start_time);

  if (i < bfs_size) {
    if (out) cout << "i < bfs_size: " << i << " " << now << " " << ret << " " << timestamp - last_update_ << " " << timestamp - last_update_ + 
			(now - 1 + ((1 << kk_) - 1) - (int)ret + (i - 1) * ((1 << kk_) - 1)) * window_time_ +
			  window_time_ / 2  << endl;
    return timestamp - last_update_ + 
			(((1 << kk_) - 1) - (int)ret + (i - 1) * ((1 << kk_) - 1) + now - 1) * window_time_ +
			window_time_ / 2;
  }
  else {
    if (out) cout << "i == bfs_size: " << timestamp - last_update_ << " " << estimateTimeByOldBfs(flowkey) << endl;

    return timestamp - last_update_ + (i - 1) * ((1 << kk_) - 1) * window_time_ +
      estimateTimeByOldBfs(flowkey);
  }
}

template <typename hash_t>
template <int32_t key_len>
uint64_t
MergeableFDFilter<hash_t>::estimateTimeByOldBfs(const FlowKey<key_len> &flowkey) {
  uint64_t interval = 0;
  auto iter = old_bfs_.begin();
  int ret = 0;
  for (; iter != old_bfs_.end(); ++iter) {
    if ((ret = iter->first.query(flowkey) != 0)) {
      break;
    }
    interval += pow(2, iter->second) * ((1 << kk_) - 1) * window_time_;
  }
  if (iter != old_bfs_.end()) {
    // if (iter->second == 0)
    //   interval += (((1 << kk_) - 1) - ret) * window_time_ + window_time_ / 2;
    // else
      interval += pow(2, iter->second) * ((1 << kk_) - 1) * window_time_ / 2;
  }
  
  return interval;
}

template <typename hash_t>
void MergeableFDFilter<hash_t>::mergeOldBfs(const BitBf<hash_t> &bf) {
  old_bfs_.push_front(std::make_pair(bf, 0));
  if (old_bfs_.size() < 3) {
    return;
  }
  auto it1 = old_bfs_.begin();
  auto it2 = old_bfs_.begin();
  auto it3 = old_bfs_.begin();
  ++it2;
  ++it3;
  ++it3;
  while (it3 != old_bfs_.end()) {
    if (it1->second == it2->second && it2->second == it3->second) {
      it2->first.Or(it3->first);
      it2->second += 1;
      old_bfs_.erase(it3);
      ++it1;
      ++it2;
      if (it2 == old_bfs_.end()) {
        break;
      }
      it3 = next(it2, 1);
    } else {
      break;
    }
  }
}

template <typename hash_t> size_t MergeableFDFilter<hash_t>::size() const {
	std::cout << "MergeableFDFilter size: " << gbf_.size() << " " << empty_bf_.size() << " " <<  bfs_.size() << " " <<  old_bfs_.size() << " " << kk_ << std::endl;
  return gbf_.size() + empty_bf_.size() * bfs_.size() + empty_bf_.size() * old_bfs_.size() / kk_;
}

template <typename hash_t> 
auto MergeableFDFilter<hash_t>::clear() -> void {
  for(auto &bf : bfs_){
    bf.clear();
  }
}

} // namespace sketch

#endif