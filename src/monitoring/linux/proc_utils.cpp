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
#include <sys/types.h>

#include <fstream>
#include <list>
#include <string>

#include "common/foreach.hpp"
#include "common/seconds.hpp"
#include "common/try.hpp"
#include "common/utils.hpp"

#include "monitoring/linux/proc_utils.hpp"
#include "monitoring/process_stats.hpp"

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


Try<ProcessStats> getProcessStats(const pid_t& pid)
{
  string procPath = "/proc/" + utils::stringify(pid) + "/stat";

  ifstream pStatFile(procPath.c_str());
  if (!pStatFile.is_open()) {
    return Try<ProcessStats>::error("Cannot open " + procPath + " for stats");
  }

  // Dummy vars for leading entries in stat that we don't care about.
  string comm, state, tty_nr, tpgid, flags, minflt, cminflt, majflt, cmajflt;
  string cutime, cstime, priority, nice, num_threads, itrealvalue, vsize;

  // These are the fields we want.
  double rss, utime, stime, starttime;
  pid_t _pid, ppid, pgrp, sid;

  // Parse all fields from stat.
  pStatFile >> _pid >> comm >> state >> ppid >> pgrp >> sid >> tty_nr >>
               tpgid >> flags >> minflt >> cminflt >> majflt >> cmajflt >>
               utime >> stime >> cutime >> cstime >> priority >> nice >>
               num_threads >> itrealvalue >> starttime >> vsize >> rss;

  // Check for any read/parse errors.
  if (!pStatFile) {
    return Try<ProcessStats>::error("Failed to read ProcessStats from proc");
  }

  Try<seconds> bootTime = getBootTime();
  if (bootTime.isError()) {
    return Try<ProcessStats>::error(bootTime.error());
  }

  // TODO(adegtiar): consider doing something more sophisticated for memUsage.
  return ProcessStats(_pid, ppid, pgrp, sid,
      seconds(ticksToSeconds(utime + stime)),
      seconds(bootTime.get().value + jiffiesToSeconds(starttime).value),
      rss * sysconf(_SC_PAGE_SIZE));
}


Try<seconds> getBootTime()
{
  pthread_once(&isBootTimeInitialized, initCachedBootTime);
  return cachedBootTime;
}


Try<seconds> getStartTime(const pid_t& pid)
{
  Try<ProcessStats> pStats = getProcessStats(pid);
  if (pStats.isSome()) {
    return pStats.get().startTime;
  } else {
    return Try<seconds>::error(pStats.error());
  }
}


Try<list<pid_t> > getAllPids()
{
  list<pid_t> pids = list<pid_t>();

  foreach (const string& filename, utils::os::listdir("/proc")) {
    Try<pid_t> next_pid = utils::numify<pid_t>(filename);
    if (next_pid.isSome()) {
      pids.push_back(next_pid.get());
    }
  }

  if (!pids.empty()) {
    return pids;
  } else {
    return Try<list<pid_t> >::error("Failed to retrieve pids from proc");
  }
}

} // namespace monitoring {
} // namespace internal {
} // namespace mesos {
