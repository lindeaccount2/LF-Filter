#include "common/BOBHash.hh"
#include "common/test.hh"
#include "sketch/LFFilter.hh"
#include <algorithm>
#include <string>
#include <vector>
using std::string;
using std::vector;

void testLFFilter(std::shared_ptr<INIReader> config,
                        const vector<util::Record> &records);


int main(int argc, char *argv[]) {

  auto config = util::read_ini("../config.ini");
  string data_file = config->Get("COMMON", "data_file", "");
  auto records = util::readTrace(data_file);
  std::set<FlowKey<13>> s;
  std::transform(records.begin(), records.end(), std::inserter(s, s.begin()),
                 [](auto &record) { return record.flowkey_; });
  printf("flow num is %ld \n", s.size());

  testLFFilter(config, records);

  return 0;
}

void testLFFilter(std::shared_ptr<INIReader> config, const vector<util::Record> &records) {
    std::cout << "\n>>> Testing LF-Filter <<<" << std::endl;

    uint64_t delay_thres = config->GetInteger("COMMON", "delay_thres", 600000);
    uint64_t jitter_thres = config->GetInteger("COMMON", "jitter_thres", 100000);

    int n_hot = config->GetInteger("LFFilter", "n_hot", 10);
    int n_warm = config->GetInteger("LFFilter", "n_warm", 16);
    int n_cold = config->GetInteger("LFFilter", "n_cold", 4);
    int k_w = config->GetInteger("LFFilter", "k_w", 5);

    int heavy_thres = config->GetInteger("LFFilter", "heavy_thres", 5);

    int slots = config->GetInteger("LFFilter", "slots", 0);
    int fp_bits = config->GetInteger("LFFilter", "fp_bits", 4);
    int win_id_bits = config->GetInteger("LFFilter", "win_id_bits", 2);
    sketch::LFFilter<hash::AwareHash> lff(n_hot, n_warm, n_cold, k_w, slots, fp_bits, win_id_bits, delay_thres, heavy_thres);

    lff.printMemoryBreakdown();

    standardTest(lff, records, delay_thres, jitter_thres);
}
