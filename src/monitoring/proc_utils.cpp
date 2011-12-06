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
#include <fstream>
#include <glog/logging.h>
#include <iostream>
#include <pthread.h>
#include <sstream>
#include <string>
#include <sys/time.h>
#include <vector>

#include "common/foreach.hpp"
#include "common/try.hpp"
#include "common/utils.hpp"
#include "proc_utils.hpp"

using std::ifstream;
using std::string;
using std::stringstream;
using std::vector;

namespace mesos {
namespace internal {
namespace monitoring {

// Code for initializing cached boot time.
static pthread_once_t isBootTimeInitialized = PTHREAD_ONCE_INIT;
static Try<double> cachedBootTime = Try<double>::error("not initialized");

void initCachedBootTime() {
  string line;
  ifstream statFile("/proc/stat");
  if (statFile.is_open()) {
    while (statFile.good()) {
      getline (statFile, line);
      if (line.compare(0, 6, "btime ") == 0) {
        Try<double> bootTime = utils::numify<double>(line.substr(6));
        if (bootTime.isSome()) {
          cachedBootTime = bootTime.get() * 1000.0;
          return;
        }
      }
    }
  }
  cachedBootTime = Try<double>::error("unable to read boot time from proc");
}

// Converts time in jiffies to milliseconds.
inline double jiffiesToMillis(double jiffies)
{
  return jiffies * 1000.0 / HZ;
}

// Converts time in system ticks (as defined by _SC_CLK_TCK, NOT CPU
// clock ticks) to milliseconds.
inline double ticksToMillis(double ticks)
{
  return ticks * 1000.0 / sysconf(_SC_CLK_TCK);
}

Try<ProcessStats> getProcessStats(const string& pid)
{
  string procPath = "/proc/" + pid + "/stat";
  ifstream statStream(procPath.c_str());
  if (statStream.is_open()) {
    ProcessStats pinfo;
    // Dummy vars for leading entries in stat that we don't care about.
    string comm, state, tty_nr, tpgid, flags, minflt, cminflt, majflt, cmajflt;
    string cutime, cstime, priority, nice, O, itrealvalue, vsize;
    // These are the fields we want.
    double rss, utime, stime, starttime;
    // Parse all fields from stat.
    statStream >> pinfo.pid >> comm >> state >> pinfo.ppid >> pinfo.pgrp
                >> pinfo.session >> tty_nr >> tpgid >> flags >> minflt
                >> cminflt >> majflt >> cmajflt >> utime >> stime >> cutime
                >> cstime >> priority >> nice >> O >> itrealvalue
                >> starttime >> vsize >> rss;
    Try<double> bootTime = getBootTime();
    if (bootTime.isError()) {
      return Try<ProcessStats>::error(bootTime.error());
    }
    pinfo.startTime = bootTime.get() + jiffiesToMillis(starttime);
    pinfo.memUsage = rss * sysconf(_SC_PAGE_SIZE);
    pinfo.cpuTime = ticksToMillis(utime + stime);
    return pinfo;
  } else {
    return Try<ProcessStats>::error("Cannot open " + procPath + " for stats");
  }
}

Try<double> getBootTime()
{
  pthread_once(&isBootTimeInitialized, initCachedBootTime);
  return cachedBootTime;
}

double getCurrentTime()
{
  timeval ctime;
  gettimeofday(&ctime, NULL);
  return (ctime.tv_sec * 1000.0 + ctime.tv_usec / 1000.0);
}

Try<double> getStartTime(const string& pid)
{
  Try<ProcessStats> pStats = getProcessStats(pid);
  if (pStats.isError()) {
    return Try<double>::error(pStats.error());
  } else {
    return pStats.get().startTime;
  }
}

vector<string> getAllPids() {
  vector<string> pids = vector<string>();
  foreach (const string& filename, utils::os::listdir("/proc")) {
    if (utils::numify<uint64_t>(filename).isSome()) {
      pids.push_back(filename);
    }
  }
  return pids;
}

} // namespace monitoring {
} // namespace internal {
} // namespace mesos {
