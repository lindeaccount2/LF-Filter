#ifndef SKETCH_OPTMERGEABLEFDFILTER_HH
#define SKETCH_OPTMERGEABLEFDFILTER_HH

#include "common/flowkey.hh"
#include "common/hash.hh"
#include "sketch/BitBloomFilter.hh"
#include <algorithm>
#include <limits>
#include <string>

namespace sketch {
template <typename hash_t>
class OptMergeableFDFilter {
private:
  BloomFilter<hash_t> gbf_;
  
  int k_;	       // k   个 Bit Bloom Filter (Counter 含多个比特)
  int kk_;         // kk_ 个 Bit in Counter
  int part;        // 时间窗口个数

  uint64_t *bfs_;            // Bit Bloom Filter 存储组，每组为 1Block * 8BF
  int block_num;             // Bit Bloom Filter 的 Block 数量
  int group_num_per_block;   // 每个 Block（每行）的 Bit Bloom Filter Group 数量
  int ncounters_;
  int num_hash_;
  hash_t *hash_fns_;         // 哈希函数
  
  int current_bf_index = 1;  // 当前更新的 Bloom Filter 的索引
  int sub_win_num = 0;       // 已经使用的 subwindow 的总数
  int *current_counter_indexes;  // 当前查询的 key 的 num_hash 个哈希结果
  bool full_flag = false;        // 是否所有的 Bit Bloom Filter 都被插满过
  
  uint64_t start_time_;
  uint64_t delay_thres_;
  uint64_t last_update_;

  int merge_state = 0;

  uint64_t thput_num_;
  std::chrono::nanoseconds insertion_time_;
  std::chrono::nanoseconds query_time_;

public:
  OptMergeableFDFilter(int k, int kk, int ncounters, int num_hash, int gnbits, int gnum_hash, uint64_t delay_thres);
  template <int32_t key_len>
  uint64_t update(const FlowKey<key_len> &flowkey, uint64_t timestamp);
  template <int32_t key_len>
  void insertBloomFilter(int bf_index, const FlowKey<key_len> &flowkey, int value);
  template <int32_t key_len>
  int queryKey(const FlowKey<key_len> &flowkey);
  bool checkGroup(int group_id);
  void clearBloomFilter(int bf_index);
  ~OptMergeableFDFilter();

  uint8_t tmpTestKey[13] = {140, 200, 69, 215, 64, 5, 155, 64, 0, 80, 217, 230, 6};
  FlowKey<13> testKey = FlowKey<13>(tmpTestKey);

  void setInitTime(uint64_t timestamp) { last_update_ = start_time_ = timestamp; };
  size_t size() const;
  auto clear() -> void;

  auto insertion_throughput() -> double {
    return (1.0 * thput_num_ / 1e6) / (1.0 * insertion_time_.count() / 1e9);
  }
  auto query_throughput() -> double {
    return (1.0 * thput_num_ / 1e6) / (1.0 * query_time_.count() / 1e9);
  }

  std::string name() { return "OptMergeableFDFilter"; };
};

template <typename hash_t>
OptMergeableFDFilter<hash_t>::OptMergeableFDFilter(int k, int kk, int ncounters, int num_hash, 
							   int gnbits, int gnum_hash, uint64_t delay_thres)
    :k_(k), kk_(kk), ncounters_(ncounters), delay_thres_(delay_thres), gbf_(gnbits, gnum_hash) {
	
	block_num = (1+(ncounters>>2));
	group_num_per_block = (1+(k>>3));
	part = k * ((1 << kk) - 1);
	
	bfs_ = new uint64_t[block_num * group_num_per_block];
	num_hash_ = num_hash;
	hash_fns_ = new hash_t[num_hash];
	current_counter_indexes = new int[num_hash_];
	
	current_bf_index = 1;
	sub_win_num = 0;
	full_flag = false;

	merge_state = 0;

	insertion_time_ = std::chrono::nanoseconds::zero();
	query_time_ = std::chrono::nanoseconds::zero();
	thput_num_ = 0;
}

template <typename hash_t> 
OptMergeableFDFilter<hash_t>::~OptMergeableFDFilter() {}

template <typename hash_t>
template <int32_t key_len>
void OptMergeableFDFilter<hash_t>::insertBloomFilter(int bf_index, const FlowKey<key_len> &flowkey, int value) {
	// Counter 在 bfs_[block_id][group_id] 存储组中第block_offset行与第group_offset列
	int group_id = bf_index >> 3;
	int group_offset = bf_index - (group_id << 3);
	int block_id = 0;
	int counter_id = 0;
	int block_offset = 0;

	auto start_point = std::chrono::steady_clock::now();
	for (int i=0; i<num_hash_; ++i) {
		counter_id = hash_fns_[i](flowkey) % ncounters_;
		//counter_id = (*flowkey.cKey()) % ncounters_;
		block_id = counter_id >> 2;
		block_offset = counter_id - (block_id << 2);
		bfs_[block_id*group_num_per_block+group_id] |= ((uint64_t)value << (64-block_offset*16-group_offset*kk_-kk_));
	}
	auto finish_point = std::chrono::steady_clock::now();
	insertion_time_ += std::chrono::duration_cast<std::chrono::nanoseconds>(finish_point - start_point);
	return; 
}

template <typename hash_t>
template <int32_t key_len>
int OptMergeableFDFilter<hash_t>::queryKey(const FlowKey<key_len> &flowkey) {
	auto start_point = std::chrono::steady_clock::now();
	int group_id = current_bf_index >> 3;
	int group_offset = current_bf_index - (group_id << 3);
	int block_id = 0;
	int block_offset = 0;
	uint16_t base = 0xffff;
	
	for (int i=0; i<num_hash_; ++i) {
		current_counter_indexes[i] = hash_fns_[i](flowkey) % ncounters_;
		block_id = current_counter_indexes[i] >> 2;
		block_offset = current_counter_indexes[i] - (block_id << 2);
		uint16_t* batched_counters = (uint16_t*) (&bfs_[block_id*group_num_per_block+group_id]);
		base &= (*batched_counters);
	}

	if (base != 0) {
		uint16_t check_oldest_part = 0x3fff >> (group_offset*kk_);
		base &= check_oldest_part;
		auto finish_point = std::chrono::steady_clock::now();
		query_time_ += std::chrono::duration_cast<std::chrono::nanoseconds>(finish_point - start_point);
		if (base) return -1;  // 在除了Merge部分之外最老的部分
		return 0;             // 就在本Group里 
	}

	for (int g=group_id-1; g>=0; --g) {
		if (checkGroup(g)) {
			auto finish_point = std::chrono::steady_clock::now();
			query_time_ += std::chrono::duration_cast<std::chrono::nanoseconds>(finish_point - start_point);
			return group_id-g;
		}
	}

	if (full_flag) {
		for (int g=(k_ >> 3); g>group_id; --g) {
			if (checkGroup(g)) {
				auto finish_point = std::chrono::steady_clock::now();
				query_time_ += std::chrono::duration_cast<std::chrono::nanoseconds>(finish_point - start_point);
				return (k_ >> 3)+1+group_id-g;
			}
		}
	}

	if (checkGroup(0)) return -2; // 在最老的部分

	auto finish_point = std::chrono::steady_clock::now();
	query_time_ += std::chrono::duration_cast<std::chrono::nanoseconds>(finish_point - start_point);

	return -3;  // 找不到呢QAQ
}

template <typename hash_t>
bool OptMergeableFDFilter<hash_t>::checkGroup(int group_id) {
	int block_id = 0;
	int block_offset = 0;
	uint16_t base = 0xffff;

	for (int i=0; i<num_hash_; ++i) {
		block_id = current_counter_indexes[i] >> 2;
		block_offset = current_counter_indexes[i] - (block_id << 2);
		uint16_t* batched_counters = (uint16_t*) (&bfs_[block_id*group_num_per_block+group_id]);
		base &= (*batched_counters);
	}

	if (base) return true;
	return false;
}

template <typename hash_t>
void OptMergeableFDFilter<hash_t>::clearBloomFilter(int bf_index) {
	int group_id = bf_index >> 3;
	int group_offset = bf_index - (group_id << 3);
	
	uint64_t base = 0xc000c000c000c000;
	for (int i=0; i<block_num; ++i) {
		// Compression
		base &= (bfs_[i*group_num_per_block+group_id] << (group_id << 1));
		base |= (base << 1);
		// Merge
		base &= 0x8000800080008000;
		bfs_[i*group_num_per_block] |= base;
	}
	
	// Clear
	base = ((1<<kk_) - 1) << ((7-group_offset)*kk_);
	base |= (base << 16);
	base |= (base << 32);
	base = ~(base);

	for (int i=0; i<block_num; ++i) {
		bfs_[i*group_num_per_block+group_id] &= base;
	}
	return;
}

template <typename hash_t>
template <int32_t key_len>
uint64_t OptMergeableFDFilter<hash_t>::update(const FlowKey<key_len> &flowkey, uint64_t timestamp) {
	// 下一个时间窗口
	++thput_num_;
	auto start_point = std::chrono::steady_clock::now();
	if ((timestamp - last_update_) * part >= delay_thres_) {
		last_update_ = timestamp;
		++sub_win_num;
		// 要使用下一个 Bloom Filter
		if (sub_win_num % ((1 << kk_) - 1) == 0) {
			current_bf_index = (current_bf_index % k_) + 1;
			if (full_flag) clearBloomFilter(current_bf_index);
			if (current_bf_index==k_) full_flag = true;
		}
	}

	int now = sub_win_num % ((1 << kk_) - 1) + 1;

	// 如果是第一个报文
	if (!gbf_.query(flowkey)) {
		gbf_.insert(flowkey);
		auto finish_point = std::chrono::steady_clock::now();
		insertion_time_ += std::chrono::duration_cast<std::chrono::nanoseconds>(finish_point - start_point);
		insertBloomFilter(current_bf_index, flowkey, now);
		return 0;
	}

	int query_result = queryKey(flowkey);
	insertBloomFilter(current_bf_index, flowkey, now);

	if (query_result<0) return 1;
	else return 0;
}

template <typename hash_t> 
size_t OptMergeableFDFilter<hash_t>::size() const {
	return sizeof(bfs_[0]) * block_num * group_num_per_block + gbf_.size();
}

template <typename hash_t> 
auto OptMergeableFDFilter<hash_t>::clear() -> void {
    memset(bfs_, 0, sizeof(uint64_t)*block_num*group_num_per_block);
	merge_state = 0;
	thput_num_ = 0;
	insertion_time_ = std::chrono::nanoseconds::zero();
	query_time_ = std::chrono::nanoseconds::zero();
}

} // namespace sketch

#endif