#ifndef SKETCH_BLOOMFILTER_HH
#define SKETCH_BLOOMFILTER_HH

#include "common/hash.hh"
#include "common/util.hh"
#include <algorithm>
#include <cstddef>
#include <string>

namespace sketch {
template <typename hash_t> class BloomFilter {

private:
  int nbits_;
  int num_hash_;
  int nbytes_;
  uint8_t *arr_;
  hash_t *hash_fns_;

  static inline int BYTE(int n) { return n / 8; }
  static inline int BIT(int n) { return n % 8; }
  inline void setBit(int pos) { arr_[BYTE(pos)] |= 1 << (7 - BIT(pos)); }
  inline void resetBit(int pos) { arr_[BYTE(pos)] &= ~(1 << (7 - BIT(pos))); }
  inline uint8_t getBit(int pos) const {
    return (arr_[BYTE(pos)] >> (7 - BIT(pos))) & 1;
  }

public:
  BloomFilter(int nbits, int num_hash);
  BloomFilter();
  ~BloomFilter();
  BloomFilter(const BloomFilter<hash_t> &);
  BloomFilter(BloomFilter<hash_t> &&) noexcept;
  BloomFilter<hash_t> &operator=(BloomFilter<hash_t>) noexcept;
  void swap(BloomFilter<hash_t> &bf) noexcept;
  template <int32_t key_len> void insert(const FlowKey<key_len> &flowkey);
  template <int32_t key_len> void reset(const FlowKey<key_len> &flowkey);
  template <int32_t key_len> bool query(const FlowKey<key_len> &flowkey) const;
  std::size_t size() const;
  void clear();
  std::string name() { return "BloomFilter"; };
  static int getNbitsBySize(int num_hash, int mem_size);
  void And(const BloomFilter<hash_t> &rhs);
  void Or(const BloomFilter<hash_t> &rhs);
};

template <typename hash_t>
BloomFilter<hash_t>::BloomFilter()
    : nbits_(0), num_hash_(0), nbytes_(0), arr_(nullptr), hash_fns_(nullptr) {}

template <typename hash_t>
BloomFilter<hash_t>::BloomFilter(int nbits, int num_hash)
    : nbits_(nbits), num_hash_(num_hash) {

  nbits_ = util::NextPrime(nbits_);
  nbytes_ = (nbits_ & 7) == 0 ? (nbits_ >> 3) : (nbits_ >> 3) + 1;
  hash_fns_ = new hash_t[num_hash_];

  arr_ = new uint8_t[nbytes_]();
  std::fill(arr_, arr_ + nbytes_, 0);
}
template <typename hash_t> BloomFilter<hash_t>::~BloomFilter() {
  delete[] hash_fns_;
  delete[] arr_;
}

template <typename hash_t>
BloomFilter<hash_t>::BloomFilter(const BloomFilter &bf) {
  nbits_ = bf.nbits_;
  nbytes_ = bf.nbytes_;
  num_hash_ = bf.num_hash_;
  hash_fns_ = new hash_t[num_hash_];
  std::copy(bf.hash_fns_, bf.hash_fns_ + num_hash_, hash_fns_);
  arr_ = new uint8_t[nbytes_]();
  std::copy(bf.arr_, bf.arr_ + nbytes_, arr_);
}

template <typename hash_t>
BloomFilter<hash_t>::BloomFilter(BloomFilter<hash_t> &&bf) noexcept {
  hash_fns_ = bf.hash_fns_;
  bf.hash_fns_ = nullptr;
  arr_ = bf.arr_;
  bf.arr_ = nullptr;
  nbits_ = bf.nbits_;
  nbytes_ = bf.nbytes_;
  num_hash_ = bf.num_hash_;
}

template <typename hash_t>
BloomFilter<hash_t> &
BloomFilter<hash_t>::operator=(BloomFilter<hash_t> bf) noexcept {
  bf.swap(*this);
  return *this;
}

template <typename hash_t>
void BloomFilter<hash_t>::swap(BloomFilter<hash_t> &bf) noexcept {
  using std::swap;
  swap(nbits_, bf.nbits_);
  swap(nbytes_, bf.nbytes_);
  swap(num_hash_, bf.num_hash_);
  swap(hash_fns_, bf.hash_fns_);
  swap(arr_, bf.arr_);
}

template <typename hash_t>
template <int32_t key_len>
void BloomFilter<hash_t>::insert(const FlowKey<key_len> &flowkey) {

  for (int i = 0; i < num_hash_; ++i) {
    int idx = hash_fns_[i](flowkey) % nbits_;

    setBit(idx);
  }

}

template <typename hash_t>
template <int32_t key_len>
void BloomFilter<hash_t>::reset(const FlowKey<key_len> &flowkey) {

  for (int i = 0; i < num_hash_; ++i) {
    int idx = hash_fns_[i](flowkey) % nbits_;

    resetBit(idx);
  }

}

template <typename hash_t>
template <int32_t key_len>
bool BloomFilter<hash_t>::query(const FlowKey<key_len> &flowkey) const {
  for (int i = 0; i < num_hash_; ++i) {
    int idx = hash_fns_[i](flowkey) % nbits_;
    if (getBit(idx) == 0) {
      return false;
    }
  }
  return true;
}
template <typename hash_t> std::size_t BloomFilter<hash_t>::size() const {
  return nbytes_ * sizeof(uint8_t);
}
template <typename hash_t> void BloomFilter<hash_t>::clear() {
  std::fill(arr_, arr_ + nbytes_, 0);
}
template <typename hash_t>
int BloomFilter<hash_t>::getNbitsBySize(int num_hash, int mem_size) {
  int nbits =
      (mem_size - sizeof(BloomFilter<hash_t>) - num_hash * sizeof(hash_t)) * 8;
  return util::NearestPrime(nbits);
}

template <typename hash_t>
void BloomFilter<hash_t>::And(const BloomFilter<hash_t> &rhs) {
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
void BloomFilter<hash_t>::Or(const BloomFilter<hash_t> &rhs) {
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
}
template <typename hash_t>
void swap(sketch::BloomFilter<hash_t> &lbf,
          sketch::BloomFilter<hash_t> &rbf) noexcept {
  lbf.swap(rbf);
}
#endif