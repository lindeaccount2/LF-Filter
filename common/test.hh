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
  cout << "standard test start" << endl;
  int tp_cnt = 0;
  int tn_cnt = 0;
  int fp_cnt = 0;
  int fn_cnt = 0;
  int cnt = 0;
  int high_ifpd_cnt = 0;
  int false_detect_first_cnt = 0;
  double relative_error = 0;
  map<FlowKey<13>, uint64_t> flow_map;
  uint64_t real_delay;
  uint64_t esti_delay, last_esti_delay = 0;
  double low_delay_re = 0;
  int low_delay_cnt = 0;
  int jitter_cnt = 0;
  // Set initial time
  sketch.setInitTime(vec[0].timestamp_);

  for (auto &record : vec) {
    // if (cnt == 1000000)
    //   break;
    ++cnt;
    auto iter = flow_map.find(record.flowkey_);
    if (iter == flow_map.end()) {
      flow_map.emplace(record.flowkey_, record.timestamp_);
      real_delay = 0;
    } else {
      // update
      real_delay = record.timestamp_ - iter->second;
      iter->second = record.timestamp_;
    }


    esti_delay = sketch.update(record.flowkey_, record.timestamp_);

    if (real_delay == 0 && esti_delay != 0) {
      false_detect_first_cnt++;
    }

    if (real_delay < delay_thres && esti_delay < delay_thres) {
      tn_cnt++;
    } else if (real_delay < delay_thres && esti_delay >= delay_thres) {
      fp_cnt++;
    } else if (real_delay >= delay_thres && esti_delay < delay_thres) {
      high_ifpd_cnt++;
      fn_cnt++;
      relative_error +=
          fabs(1.0 * (int64_t(real_delay) - int64_t(esti_delay)) / real_delay);
    } else {
      high_ifpd_cnt++;
      tp_cnt++;
      relative_error +=
          fabs(1.0 * (int64_t(real_delay) - int64_t(esti_delay)) / real_delay);
    }
    if (real_delay > 1000000) {
      low_delay_re +=
          fabs(1.0 * (int64_t(real_delay) - int64_t(esti_delay)) / real_delay);
      low_delay_cnt++;
    }
  }
  low_delay_re /= low_delay_cnt;
  relative_error /= high_ifpd_cnt;
  // std::cout << tp_cnt << " ,, " << fp_cnt << ",, " << tn_cnt << " ,, " << fn_cnt << std::endl;
  double precision = 1.0 * tp_cnt / (tp_cnt + fp_cnt);
  double recall = 1.0 * tp_cnt / (tp_cnt + fn_cnt);
  double f1 = 2.0 * precision * recall / (precision + recall);
  double false_first_rate = 1.0 * false_detect_first_cnt / flow_map.size();
  cout << "name: " << sketch.name() << endl;
  cout << "size: " << sketch.size() / 1024 << "KB" << endl;
  cout << "precision: " << precision << endl;
  cout << "recall: " << recall << endl;
  cout << "f1: " << f1 << endl;
  cout << "relative_error: " << relative_error << endl;
  
  // sketch.clear();
  // for (auto &record : vec) {
  //     sketch.update(record.flowkey_, record.timestamp_);
  //     //sketch.insert(record.flowkey_, record.timestamp_);
  // }
  // cout << "insertion throughput: " << sketch.insertion_throughput() << "Mips" << endl;
  // cout << "query throughput: " << sketch.query_throughput() << "Mips" << endl;

  cout << "standard test end" << endl << endl;
}

#endif