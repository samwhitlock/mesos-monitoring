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
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/time.h>
#include <vector>

#include "proc_utils.hpp"

using std::cout;
using std::endl;
using std::ifstream;
using std::ios_base;
using std::string;
using std::stringstream;
using std::vector;

namespace mesos { namespace internal { namespace monitoring {

// TODO(adegtiar): make the procfs mount point configurable (currently assuming
// '/proc').

ProcessStats getProcessStats(const string& pid)
{
  ProcessStats pinfo = {};
  string proc_path = "/proc/" + pid + "/stat";
  ifstream stat_stream(proc_path.c_str(), ios_base::in);
  // Dummy vars for leading entries in stat that we don't care about.
  string comm, state, tty_nr, tpgid, flags, minflt, cminflt, majflt, cmajflt;
  string cutime, cstime, priority, nice, O, itrealvalue, starttime, vsize;
  // These are the fields we want.
  long rss;
  long utime, stime;
  // Parse all fields from stat.
  stat_stream >> pinfo.pid >> comm >> state >> pinfo.ppid >> pinfo.pgrp
              >> pinfo.session >> tty_nr >> tpgid >> flags >> minflt
              >> cminflt >> majflt >> cmajflt >> utime >> stime >> cutime
              >> cstime >> priority >> nice >> O >> itrealvalue
              >> pinfo.starttime >> vsize >> rss;
  stat_stream.close();
  pinfo.mem_usage = rss * sysconf(_SC_PAGE_SIZE);
  pinfo.cpu_time = utime + stime;
  return pinfo;
}

// TODO(adegtiar): make error-reporting consistent with the rest of Mesos.
double getBootTime()
{
  string line;
  long btime = 0;
  ifstream stats_file ("/proc/stat");
  if (stats_file.is_open()) {
    while (stats_file.good()) {
      getline (stats_file,line);
      if (line.compare(0, 6, "btime ") == 0) {
        stringstream btime_str(line.substr(6));
        btime_str >> btime;
        break;
      }
    }
    if (btime == 0) {
      cout << "Unable to read boot time from proc" << endl;
    }
    stats_file.close();
  } else {
    cout << "Unable to open stats file" << endl;
  }
  return btime * 1000.0;
}

double getCurrentTime()
{
  timeval ctime;
  gettimeofday(&ctime, NULL);
  return (ctime.tv_sec * 1000.0 + ctime.tv_usec / 1000.0);
}

double getStartTime(const string& pid)
{
  ProcessStats root_stats = getProcessStats(pid);
  double starttime_after_boot = root_stats.starttime * 1000.0 / HZ;
  return getBootTime() + starttime_after_boot;
}

vector<string> getAllPids() {
  vector<string> pids = vector<string>();
  string proc_dirname = "/proc";
  DIR *proc_dp;
  dirent *proc_entry;
  if((proc_dp  = opendir(proc_dirname.c_str())) == NULL) {
    cout << "Error opening " << proc_dirname << endl;
  } else {
    string filename;
    while ((proc_entry = readdir(proc_dp)) != NULL) {
      filename = string(proc_entry->d_name);
      if (filename.find_first_not_of("0123456789") == string::npos) {
        pids.push_back(filename);
      }
    }
    closedir(proc_dp);
  }
  return pids;
}

}}} // namespace mesos { namespace internal { namespace monitoring {
