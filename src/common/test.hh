#ifndef TEST_HH
#define TEST_HH
#include "common/hash.hh"
#include "common/util.hh"
#include <cmath>
#include <iostream>
#include <map>
#include <string>
#include <vector>
using std::cin;
using std::cout;
using std::endl;
using std::map;
using std::string;
using std::vector;
template <typename sketch_t>

void standardTest(sketch_t &sketch, const vector<util::Record> &vec,
                  uint64_t delay_thres, uint64_t jitter_thres) {
  int tp_cnt = 0;
  int fp_cnt = 0;
  int fn_cnt = 0;
  map<FlowKey<13>, uint64_t> flow_map;
  uint64_t real_delay;
  uint64_t esti_delay;

  sketch.setInitTime(vec[0].timestamp_);

  for (auto &record : vec) {
    auto iter = flow_map.find(record.flowkey_);
    if (iter == flow_map.end()) {
      flow_map.emplace(record.flowkey_, record.timestamp_);
      real_delay = 0;
    } else {

      real_delay = record.timestamp_ - iter->second;
      iter->second = record.timestamp_;
    }

    esti_delay = sketch.update(record.flowkey_, record.timestamp_);

    if (real_delay < delay_thres && esti_delay < delay_thres) {
    } else if (real_delay < delay_thres && esti_delay >= delay_thres) {
      fp_cnt++;
    } else if (real_delay >= delay_thres && esti_delay < delay_thres) {
      fn_cnt++;
    } else {
      tp_cnt++;
    }
  }

  double precision = 1.0 * tp_cnt / (tp_cnt + fp_cnt);
  double recall = 1.0 * tp_cnt / (tp_cnt + fn_cnt);
  double f1 = 2.0 * precision * recall / (precision + recall);
  cout << "f1: " << f1 << endl;
}

#endif