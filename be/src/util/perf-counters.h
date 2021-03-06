// Copyright (c) 2012 Cloudera, Inc. All rights reserved.

#ifndef IMPALA_UTIL_PERF_COUNTER_H
#define IMPALA_UTIL_PERF_COUNTER_H

#include <iostream>
#include <string>
#include <vector>

#include "util/debug-util.h"

// This is a utility class that aggregates counters from the kernel.  These counters
// come from different sources.  
//   - perf counter syscall (/usr/include/linux/perf_event.h")
//   - /proc/self/io: io stats
//   - /proc/self/status: memory stats
// The complexity here is that all these sources have data in a different and not
// easy to get at format.
// 
// A typical usage pattern would be:
//  PerfCounters counters;
//  counters.AddDefaultCounters();
//  counters.Snapshot("After Init");
//  <do your work>
//  counters.Snapshot("After Work");
//  counters.PrettyPrint(cout);

namespace impala {

class PerfCounters {
 public:
  enum Counter {
    PERF_COUNTER_SW_CPU_CLOCK,
    PERF_COUNTER_SW_PAGE_FAULTS,
    PERF_COUNTER_SW_CONTEXT_SWITCHES,
    PERF_COUNTER_SW_CPU_MIGRATIONS,

    PERF_COUNTER_HW_CPU_CYCLES,
    PERF_COUNTER_HW_INSTRUCTIONS,
    PERF_COUNTER_HW_CACHE_HIT,
    PERF_COUNTER_HW_CACHE_MISSES,
    PERF_COUNTER_HW_BRANCHES,
    PERF_COUNTER_HW_BRANCH_MISSES,
    PERF_COUNTER_HW_BUS_CYCLES,

    PERF_COUNTER_VM_USAGE,
    PERF_COUNTER_VM_PEAK_USAGE,
    PERF_COUNTER_RESIDENT_SET_SIZE,

    PERF_COUNTER_BYTES_READ,
    PERF_COUNTER_BYTES_WRITE,
    PERF_COUNTER_DISK_READ,       
    PERF_COUNTER_DISK_WRITE,
  };

  // Add the 'default' counters as ones to collect.  Returns false if any of those
  // counters are not available.
  // Counters cannot be added after a snapshot has been taken.
  bool AddDefaultCounters();

  // Add a specific counter to watch.  Return false if that counter is not available.
  // Counters cannot be added after a snapshot has been taken.
  bool AddCounter(Counter);

  // Take a snapshot of all the counters and store it.  The caller can specify a name
  // for the snapshot.
  void Snapshot(const std::string& name = "");
  
  // Returns the results of that snapshot
  const std::vector<int64_t>* counters(int snapshot) const;

  // Returns readable names for the added counters
  const std::vector<std::string>* counter_names() const { return &counter_names_; }

  // Prints out the names and results for all snapshots to 'out'
  void PrettyPrint(std::ostream* out) const;

  PerfCounters();
  ~PerfCounters();

 private:
  // Copy constructor and assignment not allowed
  PerfCounters(const PerfCounters&);
  PerfCounters& operator=(const PerfCounters&);

  bool InitSysCounter(Counter counter);
  bool InitProcSelfIOCounter(Counter counter);
  bool InitProcSelfStatusCounter(Counter counter);

  bool GetSysCounters(std::vector<int64_t>& snapshot);
  bool GetProcSelfIOCounters(std::vector<int64_t>& snapshot);
  bool GetProcSelfStatusCounters(std::vector<int64_t>& snapshot);

  enum DataSource {
    SYS_PERF_COUNTER, 
    PROC_SELF_IO,
    PROC_SELF_STATUS,
  };
  
  struct CounterData {
    Counter counter;
    DataSource source;
    TCounterType::type type;
    
    // DataSource specific data.  This is used to pull the counter values.
    union {
      // For SYS_PERF_COUNTER. File descriptor where the counter value is stored.
      int fd;
      // For PROC_SELF_IO.  Line number from /proc/self/io file with this counter's value
      int proc_io_line_number;
    };
    // For PROC_SELF_STATUS.  Field name for counter
    std::string proc_status_field;
  };

  std::vector<CounterData> counters_;
  std::vector<std::string> counter_names_;
  std::vector<std::string> snapshot_names_;
  std::vector<std::vector<int64_t> > snapshots_;
  // System perf counters can be grouped together.  The OS will update all grouped counters
  // at the same time.  This is useful to better correlate counter values.
  int group_fd_;
};
  
}

#endif
