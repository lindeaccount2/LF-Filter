#ifndef COMMON_UTIL_HH
#define COMMON_UTIL_HH

#include "INIReader.h"
#include "common/flowkey.hh"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>
#include <set>
#define SYN(x) (((x)&0x12) == 0x2)
#define SYN_ACK(x) (((x)&0x12) == 0x12)
#define FIN(x) (((x)&0x11) == 0x1)
#define FIN_ACK(x) (((x)&0x11) == 0x11)
namespace util {

const int MANGLE_MAGIC = 2083697005;
const int DATA_T_SIZE = 22;
class Record {
public:
  FlowKey<13> flowkey_;
  uint64_t timestamp_;
  uint8_t flag_;
  Record(uint32_t srcip, uint32_t dstip, uint16_t srcport, uint16_t dstport,
         uint8_t protocol, uint64_t timestamp, uint8_t flag)
      : flowkey_(srcip, dstip, srcport, dstport, protocol),
        timestamp_(timestamp), flag_(flag) {}
  Record() {}
  void replaceFlowKey(const FlowKey<13> &flowkey);
};

bool IsPrime(int n);
int NextPrime(int n);
int NearestPrime(int n);
std::vector<Record> readTrace(const std::string path);
std::shared_ptr<INIReader> read_ini(std::string config_file);
std::set<FlowKey<13>> readJitterAns(const std::string &path);
template <typename T> T Mangle(T key) {
  size_t n = sizeof(T);
  char *s = (char *)&key;
  for (size_t i = 0; i < (n >> 1); ++i)
    std::swap(s[i], s[n - 1 - i]);
  return key * MANGLE_MAGIC;
}
}
#endif