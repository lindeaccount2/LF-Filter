
#ifndef COMMON_HASH_HH
#define COMMON_HASH_HH
#include "common/flowkey.hh"
#include "common/util.hh"
#include <iostream>

#include <cstdint>
#include <ctime>

namespace hash {

class AwareHash {
  uint64_t init;
  uint64_t scale;
  uint64_t hardener;

public:
  static void random_seed() { /*srand((unsigned)time(NULL));*/ srand(113424723); }
  AwareHash() {
    static const int GEN_INIT_MAGIC = 388650253;
    static const int GEN_SCALE_MAGIC = 388650319;
    static const int GEN_HARDENER_MAGIC = 1176845762;
    static int index = 0;
    static uint64_t seed = 3407;
    // random_seed();
    // seed = rand();
    static AwareHash gen_hash(GEN_INIT_MAGIC, GEN_SCALE_MAGIC,
                              GEN_HARDENER_MAGIC);

    uint64_t mangled;
    mangled = util::Mangle(seed + (index++));
    init = gen_hash((unsigned char *)&mangled, sizeof(uint64_t));
    mangled = util::Mangle(seed + (index++));
    scale = gen_hash((unsigned char *)&mangled, sizeof(uint64_t));
    mangled = util::Mangle(seed + (index++));
    hardener = gen_hash((unsigned char *)&mangled, sizeof(uint64_t));

    index %= 18;

    // std::cout << seed << " " << index << std::endl;
    // std::cout <<  init << " " << scale << " " << hardener << std::endl;
  }

  AwareHash(uint64_t init, uint64_t scale, uint64_t hardener)
      : init(init), scale(scale), hardener(hardener) {}

  uint64_t operator()(const uint8_t *data, int n) const {
    uint64_t result = init;
    while (n--) {
      result *= scale;
      result += *data++;
    }
    return result ^ hardener;
  }
  template <int32_t key_len>
  uint64_t operator()(const FlowKey<key_len> &flowkey) const {
    return this->operator()(flowkey.cKey(), key_len);
  }

  bool operator==(const AwareHash &rhs) {
    return init == rhs.init && scale == rhs.scale && hardener == rhs.hardener;
  }
};

} // namespace hash

#endif
