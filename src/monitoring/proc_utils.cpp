/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <asm/param.h>
#include <pthread.h>

#include <fstream>
#include <list>
#include <string>

#include "common/foreach.hpp"
#include "common/seconds.hpp"
#include "common/try.hpp"
#include "common/utils.hpp"
#include "monitoring/proc_utils.hpp"

using std::ifstream;
using std::string;
using std::list;

namespace mesos {
namespace internal {
namespace monitoring {

// Code for initializing cached boot time.
static pthread_once_t isBootTimeInitialized = PTHREAD_ONCE_INIT;
static Try<seconds> cachedBootTime = Try<seconds>::error("not initialized");


void initCachedBootTime()
{
  string line;
  ifstream statFile("/proc/stat");
  if (statFile.is_open()) {
    while (statFile.good()) {
      getline (statFile, line);
      if (line.compare(0, 6, "btime ") == 0) {
        Try<double> bootTime = utils::numify<double>(line.substr(6));
        if (bootTime.isSome()) {
          cachedBootTime = seconds(bootTime.get());
          return;
        }
      }
    }
  }
  cachedBootTime = Try<seconds>::error("Failed to read boot time from proc");
}


// Converts time in jiffies to seconds.
static inline seconds jiffiesToSeconds(double jiffies)
{
  return seconds(jiffies / HZ);
}


// Converts time in system ticks (as defined by _SC_CLK_TCK, NOT CPU
// clock ticks) to milliseconds.
static inline seconds ticksToSeconds(double ticks)
{
  return seconds(ticks / sysconf(_SC_CLK_TCK));
}


Try<ProcessStats> getProcessStats(const string& pid)
{
  string procPath = "/proc/" + pid + "/stat";
  ifstream pStatFile(procPath.c_str());
  if (pStatFile.is_open()) {
    // Dummy vars for leading entries in stat that we don't care about.
    string comm, state, tty_nr, tpgid, flags, minflt, cminflt, majflt, cmajflt;
    string cutime, cstime, priority, nice, num_threads, itrealvalue, vsize;
    // These are the fields we want.
    double rss, utime, stime, starttime;
    string pid, ppid, pgrp, session;
    // Parse all fields from stat.
    pStatFile >> pid >> comm >> state >> ppid >> pgrp >> session >> tty_nr >>
                 tpgid >> flags >> minflt >> cminflt >> majflt >> cmajflt >>
                 utime >> stime >> cutime >> cstime >> priority >> nice >>
                 num_threads >> itrealvalue >> starttime >> vsize >> rss;
    if (!pStatFile) {
      return Try<ProcessStats>::error("Failed to read ProcessStats from proc");
    }
    Try<seconds> bootTime = getBootTime();
    if (bootTime.isError()) {
      return Try<ProcessStats>::error(bootTime.error());
    }
    // TODO(adegtiar): consider doing something more sophisticated for mem.
    return ProcessStats(pid, ppid, pgrp, session, seconds(utime + stime),
        seconds(bootTime.get().value + jiffiesToSeconds(starttime).value),
        rss * sysconf(_SC_PAGE_SIZE));
  } else {
    return Try<ProcessStats>::error("Cannot open " + procPath + " for stats");
  }
}


Try<seconds> getBootTime()
{
  pthread_once(&isBootTimeInitialized, initCachedBootTime);
  return cachedBootTime;
}


Try<seconds> getStartTime(const string& pid)
{
  Try<ProcessStats> pStats = getProcessStats(pid);
  if (pStats.isError()) {
    return Try<seconds>::error(pStats.error());
  } else {
    return pStats.get().startTime;
  }
}


Try<list<string> > getAllPids()
{
  list<string> pids = list<string>();
  foreach (const string& filename, utils::os::listdir("/proc")) {
    if (utils::numify<uint64_t>(filename).isSome()) {
      pids.push_back(filename);
    }
  }
  if (pids.empty()) {
    return Try<list<string> >::error("Failed to retrieve pids from proc");
  } else {
    return pids;
  }
}


} // namespace monitoring {
} // namespace internal {
} // namespace mesos {
