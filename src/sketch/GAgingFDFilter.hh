#ifndef SKETCH_GAgingFDFILTER_HH
#define SKETCH_GAgingFDFILTER_HH

#include "common/flowkey.hh"
#include "common/hash.hh"
#include "sketch/BitBloomFilter.hh"
#include "sketch/GAgingBloomFilter.hh"
#include <algorithm>
#include <limits>
#include <string>

namespace sketch {
template <typename hash_t>
class GAgingFDFilter {
private:
  vector<BitBf<hash_t>> bfs_;
  GAgingBloomFilter<hash_t> gbf_;
  int k_;	// k  bit bloom filter
  int kk_; // kk_ bloom filter, representing k bits
  int part;
  int sub_win_num = 0;
  int period_ = 114514;
  int insert_cnt = 0;
  int unaged = 1;
  uint64_t start_time_;
  uint64_t delay_thres_;
  uint64_t last_update_;

public:
  GAgingFDFilter(int k, int kk, int nbits, int num_hash,
  	 			int gnbits, int gnum_hash, uint64_t delay_thres, int block_size, int period);
  ~GAgingFDFilter();
  void setInitTime(uint64_t timestamp) {
    last_update_ = start_time_ = timestamp;
  };
  std::string name() { return "GAgingFDFilter"; };
  size_t size() const;
  template <int32_t key_len>
  uint64_t update(const FlowKey<key_len> &flowkey, uint64_t timestamp);
  auto clear() -> void;
  uint8_t tmpTestKey[13] = {140, 200, 69, 215, 64, 5, 155, 64, 0, 80, 217, 230, 6};
  FlowKey<13> testKey = FlowKey<13>(tmpTestKey);
};

template <typename hash_t>
GAgingFDFilter<hash_t>::GAgingFDFilter(int k, int kk, int nbits, int num_hash,
									int gnbits, int gnum_hash, uint64_t delay_thres, int block_size, int period)
    : k_(k), kk_(kk), delay_thres_(delay_thres),
    	gbf_(gnbits, gnum_hash, block_size), period_(period), 
    	bfs_(k + 1, BitBf<hash_t>(kk, nbits, num_hash, delay_thres / (k * ((1 << kk) - 1)) ) )
{
	// std::cout << "In GAgingFDFilter:" << k << " " << kk << endl;
	part = k * ((1 << kk) - 1);
	insert_cnt = 0;
}

template <typename hash_t> GAgingFDFilter<hash_t>::~GAgingFDFilter() {}

template <typename hash_t>
template <int32_t key_len>
uint64_t GAgingFDFilter<hash_t>::update(const FlowKey<key_len> &flowkey, uint64_t timestamp) {
	// if the timestamp is outside of the latest window
	if ((timestamp - last_update_) * part >= delay_thres_) {
		// std::cout << timestamp << " " << last_update_ << std::endl;
		last_update_ = timestamp;
		sub_win_num++;
		// std::cout << "Sub window number: " << sub_win_num << std::endl;
		if (sub_win_num % ((1 << kk_) - 1) == 0) { // no empty local filters
			for (int i = 0; i < k_; ++i) {
			  // exchange two bloom filter
			  bfs_[i].swap(bfs_[i + 1]);
			}
			bfs_[k_].clear();	// clear the lastest ones, which used to be the oldest before exchange
		}
	}

	insert_cnt += 1;
	if (insert_cnt % period_ == 0 && unaged) {
		//gbf_.setClearable();
		gbf_.clear();
		unaged = 0;
	}

	if (!gbf_.query(flowkey)) { // if the first packet of a flow
		//insert_cnt += 1;
		//if (insert_cnt % period_ == 0) gbf_.setClearable();
		gbf_.insert(flowkey);
		bfs_[k_].update(flowkey, sub_win_num % ((1 << kk_) - 1) + 1);
		return 0;
	}

	int i = 0;					// if not, find the previous packet
	uint64_t ret = 0;
	for (; i <= k_; ++i) {
		if ((ret = bfs_[k_ - i].query(flowkey))) {
	    	break;
		}
	}

	uint64_t interval = delay_thres_ / part;
	int now = sub_win_num % ((1 << kk_) - 1) + 1;

	bfs_[k_].update(flowkey, now);

	if (i == 0) {
		if (ret == now)
			return timestamp - last_update_;
		else
			return timestamp - last_update_ + (now - 1) * interval;
	}

	// bfs_[k_].update(flowkey, now);

	// if (flowkey == testKey)
	// std::cout << "Ret: " << ret << " " << timestamp - last_update_ << " " << now << " " << (1 << kk_) - 1 << " " << i << std::endl;

	return timestamp - last_update_ + 
			(((1 << kk_) - 1) - (int)ret + (i - 1) * ((1 << kk_) - 1) + now - 1) * interval +
			interval / 2;
}

template <typename hash_t> size_t GAgingFDFilter<hash_t>::size() const {
	// std::cout << "GAgingFDFilter size: " << bfs_[0].size() * (k_) << std::endl;
  return bfs_[0].size() * (k_ + 1) + gbf_.size();
}

template <typename hash_t> 
auto GAgingFDFilter<hash_t>::clear() -> void {
  for(auto &bf : bfs_){
    bf.clear();
  }
  insert_cnt = 0;
}

} // namespace sketch

#endif