#ifndef COMMON_FLOWKEY_HH
#define COMMON_FLOWKEY_HH

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string>
class FlowKeyOutOfRange : public std::out_of_range {
private:
public:
  FlowKeyOutOfRange(int pos, int offset, int total_len)
      : std::out_of_range("FlowKey Out of Range: pos: " + std::to_string(pos) +
                          ", offset: " + std::to_string(offset) +
                          ", total_len: " + std::to_string(total_len)) {}
};
template <int32_t key_len> class FlowKey {
private:
  uint8_t key_[key_len];

public:
  FlowKey() { std::fill_n(key_, key_len, 0); }
  FlowKey(const uint8_t *key) { std::copy(key, key + key_len, key_); }
  template <int32_t other_len> friend class FlowKey;
  template <int32_t other_len>
  FlowKey &copy(int pos, const FlowKey<other_len> &otherkey, int o_pos,
                int len) {
    if (pos + len > key_len) {
      throw FlowKeyOutOfRange(pos, len, key_len);
    }
    if (o_pos + len > other_len) {
      throw FlowKeyOutOfRange(o_pos, len, other_len);
    }
    const uint8_t *o_key = otherkey.cKey();
    std::copy(o_key + o_pos, o_key + o_pos + len, key_ + pos);
    return *this;
  }
  FlowKey &copy(int pos, const uint8_t *key, int len) {
    if (pos + len > key_len) {
      throw FlowKeyOutOfRange(pos, len, key_len);
    }
    std::copy(key, key + len, key_ + pos);
    return *this;
  }
  const uint8_t *cKey() const { return key_; }

  bool operator==(const FlowKey &otherkey) const {
    for (int i = 0; i < key_len; ++i) {
      if (key_[i] != otherkey.key_[i]) {
        return false;
      }
    }
    return true;
  }
  bool operator<(const FlowKey &otherkey) const {
    for (int i = 0; i < key_len; ++i) {
      if (key_[i] < otherkey.key_[i]) {
        return true;
      } else if (key_[i] > otherkey.key_[i]) {
        return false;
      }
    }
    return false;
  }
};

template <> class FlowKey<4> {
private:
  union {
    uint8_t key_[4];
    uint32_t ipaddr_;
  } u;

public:
  FlowKey() { u.ipaddr_ = 0; }
  FlowKey(uint32_t ipaddr) { u.ipaddr_ = ipaddr; }
  FlowKey(const uint8_t *key) { std::copy(key, key + 4, u.key_); }
  template <int32_t other_len> friend class FlowKey;
  template <int32_t other_len>
  FlowKey &copy(int pos, const FlowKey<other_len> &otherkey, int o_pos,
                int len) {
    if (pos + len > 4) {
      throw FlowKeyOutOfRange(pos, len, 4);
    }
    if (o_pos + len > other_len) {
      throw FlowKeyOutOfRange(o_pos, len, other_len);
    }
    const uint8_t *o_key = otherkey.cKey();
    std::copy(o_key + o_pos, o_key + o_pos + len, u.key_ + pos);
    return *this;
  }
  FlowKey &copy(int pos, const uint8_t *key, int len) {
    if (pos + len > 4) {
      throw FlowKeyOutOfRange(pos, len, 4);
    }
    std::copy(key, key + len, u.key_ + pos);
    return *this;
  }
  const uint8_t *cKey() const { return u.key_; }
  bool operator==(const FlowKey &otherkey) const {
    return u.ipaddr_ == otherkey.u.ipaddr_;
  }
  bool operator<(const FlowKey &otherkey) const {
    return u.ipaddr_ < otherkey.u.ipaddr_;
  }
};
template <> class FlowKey<13> {
private:
  union {
    struct {
      uint32_t srcip_;
      uint32_t dstip_;
      uint16_t srcport_;
      uint16_t dstport_;
      uint8_t protocol_;
    } s;
    uint8_t key_[13];
  } u;

public:
  FlowKey() {
    u.s.srcip_ = u.s.dstip_ = 0;
    u.s.srcport_ = u.s.dstport_ = 0;
    u.s.protocol_ = 0;
  }
  FlowKey(const uint8_t *key) { std::copy(key, key + 13, u.key_); }
  FlowKey(uint32_t srcip, uint32_t dstip, uint16_t srcport, uint16_t dstport,
          uint8_t protocol) {
    u.s.srcip_ = srcip;
    u.s.dstip_ = dstip;
    u.s.srcport_ = srcport;
    u.s.dstport_ = dstport;
    u.s.protocol_ = protocol;
  }
  template <int32_t other_len> friend class FlowKey;
  template <int32_t other_len>
  FlowKey &copy(int pos, const FlowKey<other_len> &otherkey, int o_pos,
                int len) {
    if (pos + len > 13) {
      throw FlowKeyOutOfRange(pos, len, 13);
    }
    if (o_pos + len > other_len) {
      throw FlowKeyOutOfRange(o_pos, len, other_len);
    }
    const uint8_t *o_key = otherkey.cKey();
    std::copy(o_key + o_pos, o_key + o_pos + len, u.key_ + pos);
    return *this;
  }
  FlowKey &copy(int pos, const uint8_t *key, int len) {
    if (pos + len > 13) {
      throw FlowKeyOutOfRange(pos, len, 13);
    }
    std::copy(key, key + len, u.key_ + pos);
    return *this;
  }
  inline const uint8_t *cKey() const { return u.key_; }
  bool operator==(const FlowKey &otherkey) const {
    return u.s.srcip_ == otherkey.u.s.srcip_ &&
           u.s.dstip_ == otherkey.u.s.dstip_ &&
           u.s.srcport_ == otherkey.u.s.srcport_ &&
           u.s.dstport_ == otherkey.u.s.dstport_ &&
           u.s.protocol_ == otherkey.u.s.protocol_;
  }
  bool operator<(const FlowKey &otherkey) const {
    if (u.s.srcip_ != otherkey.u.s.srcip_) {
      return u.s.srcip_ < otherkey.u.s.srcip_;
    }
    if (u.s.dstip_ != otherkey.u.s.dstip_) {
      return u.s.dstip_ < otherkey.u.s.dstip_;
    }
    if (u.s.srcport_ != otherkey.u.s.srcport_) {
      return u.s.srcport_ < otherkey.u.s.srcport_;
    }
    if (u.s.dstport_ != otherkey.u.s.dstport_) {
      return u.s.dstport_ < otherkey.u.s.dstport_;
    }
    if (u.s.protocol_ != otherkey.u.s.protocol_) {
      return u.s.protocol_ < otherkey.u.s.protocol_;
    }
    return false;
  }
};

#endif