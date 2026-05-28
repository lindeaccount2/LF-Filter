#include "common/util.hh"
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <cstdlib>
namespace util {

void Record::replaceFlowKey(const FlowKey<13> &flowkey) {
  flowkey_.copy(0, flowkey, 0, 13);
  return;
}

bool IsPrime(int n) {
  for (int i = 2; i * i <= n; ++i)
    if (n % i == 0)
      return false;
  return true;
}

int NextPrime(int n) {
  while (!IsPrime(n))
    ++n;
  return n;
}
int NearestPrime(int n) {
  int prev_prime = n - 1;
  while (!util::IsPrime(prev_prime) && prev_prime >= 2) {
    --prev_prime;
  }
  int next_prime = util::NextPrime(n);

  return (next_prime - n) > (n - prev_prime) ? prev_prime : next_prime;
}
std::vector<Record> readTrace(const std::string path) {
  std::vector<Record> vec;
  std::vector<FlowKey<13>> fvec;
  FILE *inputData = fopen(path.c_str(), "rb");
  char buffer[DATA_T_SIZE];

  printf("Reading in data...\n");
  int cnt = 0;
  while (fread(buffer, DATA_T_SIZE, 1, inputData) == 1) {
    uint32_t srcIp = *(uint32_t *)buffer;
    uint32_t dstIp = *(uint32_t *)(buffer + 4);
    uint16_t srcPort = *(uint16_t *)(buffer + 8);
    uint16_t dstPort = *(uint16_t *)(buffer + 10);
    uint8_t protocol = *(uint8_t *)(buffer + 12);
    uint64_t timestamp = (uint64_t)((*(double *)(buffer + 13)) * 1000000);
    uint8_t flag = *(uint8_t *)(buffer + 21);
    vec.push_back(
        Record(srcIp, dstIp, srcPort, dstPort, protocol, timestamp, flag));
    fvec.push_back(FlowKey<13>(srcIp, dstIp, srcPort, dstPort, protocol));
    ++cnt;
  }
  printf("Successfully read in %d packets\n", cnt);
  fclose(inputData);
  std::sort(fvec.begin(), fvec.end());
  int num_shuffle_blocks = 3;
  auto start_pos = fvec.begin();
  auto end_pos = start_pos + cnt/num_shuffle_blocks;
  for (int u=0; u<num_shuffle_blocks; ++u) {
    std::random_shuffle(start_pos, end_pos);
    start_pos = end_pos + 1;
    end_pos += cnt/num_shuffle_blocks;
  }
  for (int i=0; i<cnt; ++i) {
    vec[i].replaceFlowKey(fvec[i]);
  }
  return vec;
}
std::set<FlowKey<13>> readJitterAns(const std::string &path) {
  std::set<FlowKey<13>> ans;
  FILE *data = fopen(path.c_str(), "rb");
  uint8_t buffer[13];
  while (fread(buffer, 13, 1, data) == 1) {
    ans.emplace(FlowKey<13>{buffer});
  }
  return ans;
}
std::shared_ptr<INIReader> read_ini(std::string config_file) {
  auto config = std::make_shared<INIReader>(config_file);
  if (config->ParseError()) {
    std::cout << "Failed to parse config file" << std::endl;
    return nullptr;
  }
  return config;
}

}