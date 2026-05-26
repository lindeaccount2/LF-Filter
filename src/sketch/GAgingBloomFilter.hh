#ifndef SKETCH_GAGINGBLOOMFILTER_HH
#define SKETCH_GAGINGBLOOMFILTER_HH

#include "common/hash.hh"
#include "common/util.hh"
#include <algorithm>
#include <cstddef>
#include <string>

namespace sketch {
template <typename hash_t> class GAgingBloomFilter {

private:
  int nbits_;
  int num_hash_;
  int nbytes_;
  uint8_t *arr_;
  hash_t *hash_fns_;

  int nblocks_;
  int block_size_;
  
  uint8_t *clear_flags;

  static inline int BYTE(int n) { return n / 8; }
  static inline int BIT(int n) { return n % 8; }
  inline void setBit(int pos) { arr_[BYTE(pos)] |= 1 << (7 - BIT(pos)); }
  inline void resetBit(int pos) { arr_[BYTE(pos)] &= ~(1 << (7 - BIT(pos))); }
  inline uint8_t getBit(int pos) const {
    return (arr_[BYTE(pos)] >> (7 - BIT(pos))) & 1;
  }

public:
  GAgingBloomFilter(int nbits, int num_hash, int block_size);
  GAgingBloomFilter();
  ~GAgingBloomFilter();
  GAgingBloomFilter(const GAgingBloomFilter<hash_t> &);
  GAgingBloomFilter(GAgingBloomFilter<hash_t> &&) noexcept;
  GAgingBloomFilter<hash_t> &operator=(GAgingBloomFilter<hash_t>) noexcept;
  void swap(GAgingBloomFilter<hash_t> &bf) noexcept;
  template <int32_t key_len> void insert(const FlowKey<key_len> &flowkey);
  template <int32_t key_len> void reset(const FlowKey<key_len> &flowkey);
  template <int32_t key_len> bool query(const FlowKey<key_len> &flowkey);
  std::size_t size() const;
  void clear();
  void setClearable();
  std::string name() { return "GAgingBloomFilter"; };
  static int getNbitsBySize(int num_hash, int mem_size);
  void And(const GAgingBloomFilter<hash_t> &rhs);
  void Or(const GAgingBloomFilter<hash_t> &rhs);
};

template <typename hash_t>
GAgingBloomFilter<hash_t>::GAgingBloomFilter()
    : nbits_(0), num_hash_(0), nbytes_(0), block_size_(1), arr_(nullptr), clear_flags(nullptr), hash_fns_(nullptr) {}

template <typename hash_t>
GAgingBloomFilter<hash_t>::GAgingBloomFilter(int nbits, int num_hash, int block_size)
    : nbits_(nbits), num_hash_(num_hash), block_size_(block_size) {
  // std::cout << "In Bf\n";
  nbits_ = util::NextPrime(nbits_);
  nblocks_ = (nbits_ / block_size_) + 1;
  clear_flags = new uint8_t[nblocks_];
  for (int i=0; i<nblocks_; ++i) {
    clear_flags[i] = 0;
  }
  nbytes_ = (nbits_ & 7) == 0 ? (nbits_ >> 3) : (nbits_ >> 3) + 1;
  hash_fns_ = new hash_t[num_hash_];
  // Allocate memory
  arr_ = new uint8_t[nbytes_]();
  std::fill(arr_, arr_ + nbytes_, 0);
}
template <typename hash_t> GAgingBloomFilter<hash_t>::~GAgingBloomFilter() {
  delete[] hash_fns_;
  delete[] arr_;
  delete[] clear_flags;
}

template <typename hash_t>
GAgingBloomFilter<hash_t>::GAgingBloomFilter(const GAgingBloomFilter &bf) {
  nbits_ = bf.nbits_;
  nbytes_ = bf.nbytes_;
  num_hash_ = bf.num_hash_;
  nblocks_ = bf.nblocks_;
  block_size_ = bf.block_size_;
  clear_flags = new uint8_t[nblocks_];
  std::copy(bf.clear_flags, bf.clear_flags+nblocks_, clear_flags);
  hash_fns_ = new hash_t[num_hash_];
  std::copy(bf.hash_fns_, bf.hash_fns_ + num_hash_, hash_fns_);
  arr_ = new uint8_t[nbytes_]();
  std::copy(bf.arr_, bf.arr_ + nbytes_, arr_);
}

template <typename hash_t>
GAgingBloomFilter<hash_t>::GAgingBloomFilter(GAgingBloomFilter<hash_t> &&bf) noexcept {
  hash_fns_ = bf.hash_fns_;
  bf.hash_fns_ = nullptr;
  arr_ = bf.arr_;
  bf.arr_ = nullptr;
  nbits_ = bf.nbits_;
  nbytes_ = bf.nbytes_;
  num_hash_ = bf.num_hash_;
  nblocks_ = bf.nblocks_;
  block_size_ = bf.block_size_;
  clear_flags = bf.clear_flags;
  bf.clear_flags = nullptr;
}

template <typename hash_t>
GAgingBloomFilter<hash_t> &
GAgingBloomFilter<hash_t>::operator=(GAgingBloomFilter<hash_t> bf) noexcept {   // 为什么是swap？
  bf.swap(*this);
  return *this;
}

template <typename hash_t>
void GAgingBloomFilter<hash_t>::swap(GAgingBloomFilter<hash_t> &bf) noexcept {
  using std::swap;
  swap(nbits_, bf.nbits_);
  swap(nbytes_, bf.nbytes_);
  swap(num_hash_, bf.num_hash_);
  swap(hash_fns_, bf.hash_fns_);
  swap(arr_, bf.arr_);
  swap(nblocks_, bf.nblocks_);
  swap(block_size_, bf.block_size_);
  swap(clear_flags, bf.clear_flags);
}

template <typename hash_t>
template <int32_t key_len>
void GAgingBloomFilter<hash_t>::insert(const FlowKey<key_len> &flowkey) {
  for (int i = 0; i < num_hash_; ++i) {
    int idx = hash_fns_[i](flowkey) % nbits_;
    int block_id = idx / block_size_;
    if (clear_flags[block_id]) {
      //std::cout << "clearing..." << std::endl;
      for (int j=block_id*block_size_; (j<(block_id*block_size_+block_size_))&&(j<nbits_); ++j) {
        resetBit(j);
      }
      clear_flags[block_id] = 0;
    }
    setBit(idx);
  }
}

template <typename hash_t>
template <int32_t key_len>
void GAgingBloomFilter<hash_t>::reset(const FlowKey<key_len> &flowkey) {
  // std::cout << "In reset \n";
  for (int i = 0; i < num_hash_; ++i) {
    int idx = hash_fns_[i](flowkey) % nbits_;
    // std::cout << idx << " ";
    resetBit(idx);
  }
  // std::cout << std::endl;
}

template <typename hash_t>
template <int32_t key_len>
bool GAgingBloomFilter<hash_t>::query(const FlowKey<key_len> &flowkey) {
  bool result = true;

  for (int i = 0; i < num_hash_; ++i) {
    int idx = hash_fns_[i](flowkey) % nbits_;
    if (getBit(idx) == 0) {
      result = false;
      break;
    }
  }
  /*
  for (int i = 0; i < num_hash_; ++i) {
    int idx = hash_fns_[i](flowkey) % nbits_;
    int block_id = idx / block_size_;
    if (clear_flags[block_id]) {
      for (int j=block_id*block_size_; (j<(block_id*block_size_+block_size_))&&(j<nbits_); ++j) {
        resetBit(j);
      }
      clear_flags[block_id] = 0;
    }
  }
  //*/
  return result;
}

template <typename hash_t> void GAgingBloomFilter<hash_t>::setClearable() {
  for (int i=0; i<nblocks_; ++i) {
    clear_flags[i] = 1;
  }
  return;
}

template <typename hash_t> std::size_t GAgingBloomFilter<hash_t>::size() const {
  return nbytes_ * sizeof(uint8_t);
}
template <typename hash_t> void GAgingBloomFilter<hash_t>::clear() {
  std::fill(arr_, arr_ + nbytes_, 0);
}
template <typename hash_t>
int GAgingBloomFilter<hash_t>::getNbitsBySize(int num_hash, int mem_size) {         // 这个的计算是？
  int nbits =
      (mem_size - sizeof(GAgingBloomFilter<hash_t>) - num_hash * sizeof(hash_t)) * 8;
  return util::NearestPrime(nbits);
}

template <typename hash_t>
void GAgingBloomFilter<hash_t>::And(const GAgingBloomFilter<hash_t> &rhs) {   // assert 都做一遍太费时间
  assert(nbits_ == rhs.nbits_);
  assert(&rhs != this);
  assert(num_hash_ == rhs.num_hash_);
  for (int i = 0; i < num_hash_; ++i) {
    assert(hash_fns_[i] == rhs.hash_fns_[i]);
  }
  for (int i = 0; i < nbytes_; ++i) {
    arr_[i] &= rhs.arr_[i];
  }
}

template <typename hash_t>
void GAgingBloomFilter<hash_t>::Or(const GAgingBloomFilter<hash_t> &rhs) {
  assert(nbits_ == rhs.nbits_);
  assert(&rhs != this);
  assert(num_hash_ == rhs.num_hash_);
  for (int i = 0; i < num_hash_; ++i) {
    assert(hash_fns_[i] == rhs.hash_fns_[i]);
  }
  for (int i = 0; i < nbytes_; ++i) {
    arr_[i] |= rhs.arr_[i];
  }
}
} // namespace sketch
template <typename hash_t>
void swap(sketch::GAgingBloomFilter<hash_t> &lbf,
          sketch::GAgingBloomFilter<hash_t> &rbf) noexcept {
  lbf.swap(rbf);
}
#endif