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
#include <sstream>
#include <string>
#include <sys/time.h>
#include <vector>

#include "common/foreach.hpp"
#include "common/utils.hpp"

#include "proc_utils.hpp"

using std::ifstream;
using std::ios_base;
using std::string;
using std::stringstream;
using std::vector;

namespace mesos {
namespace internal {
namespace monitoring {

ProcessStats getProcessStats(const string& pid)
{
  ProcessStats pinfo = {};
  string procPath = "/proc/" + pid + "/stat";
  ifstream statStream(procPath.c_str(), ios_base::in);
  if (statStream.is_open()) {
    // Dummy vars for leading entries in stat that we don't care about.
    string comm, state, tty_nr, tpgid, flags, minflt, cminflt, majflt, cmajflt;
    string cutime, cstime, priority, nice, O, itrealvalue, vsize;
    // These are the fields we want.
    long rss, utime, stime;
    // Parse all fields from stat.
    statStream >> pinfo.pid >> comm >> state >> pinfo.ppid >> pinfo.pgrp
                >> pinfo.session >> tty_nr >> tpgid >> flags >> minflt
                >> cminflt >> majflt >> cmajflt >> utime >> stime >> cutime
                >> cstime >> priority >> nice >> O >> itrealvalue
                >> pinfo.startTime >> vsize >> rss;
    statStream.close();
    pinfo.memUsage = rss * sysconf(_SC_PAGE_SIZE);
    pinfo.cpuTime = utime + stime;
  } else {
    LOG(ERROR) << "Cannot open " << procPath << " to get stats";
  }
  return pinfo;
}

double getBootTime()
{
  string line;
  long bootTime = 0;
  ifstream statFile("/proc/stat");
  if (statFile.is_open()) {
    while (statFile.good()) {
      getline (statFile, line);
      if (line.compare(0, 6, "btime ") == 0) {
        stringstream bootTimeString(line.substr(6));
        bootTimeString >> bootTime;
        break;
      }
    }
    if (bootTime == 0) {
      LOG(ERROR) << "Unable to read boot time from proc";
    }
    statFile.close();
  } else {
    LOG(ERROR) << "Unable to open stat file";
  }
  return bootTime * 1000.0;
}

double getCurrentTime()
{
  timeval ctime;
  gettimeofday(&ctime, NULL);
  return (ctime.tv_sec * 1000.0 + ctime.tv_usec / 1000.0);
}

double getStartTime(const string& pid)
{
  bootJiffiesToMillis(getProcessStats(pid).startTime);
}

double bootJiffiesToMillis(double jiffies)
{
  double startTimeAfterBoot = jiffies * 1000.0 / HZ;
  return getBootTime() + startTimeAfterBoot;
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
