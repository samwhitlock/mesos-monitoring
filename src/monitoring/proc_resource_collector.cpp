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

#include <iostream>
#include <string>
#include <vector>

#include "common/foreach.hpp"
#include "common/resources.hpp"
#include "common/try.hpp"

#include "monitoring/proc_resource_collector.hpp"
#include "proc_utils.hpp"

using std::ios_base;
using std::string;
using std::vector;

namespace mesos { namespace internal { namespace monitoring {

ProcResourceCollector::ProcResourceCollector(const string& _rootPid)
  : rootPid(_rootPid), initialized(false) {}

ProcResourceCollector::~ProcResourceCollector() {}

Try<double> ProcResourceCollector::getMemoryUsage()
{
}

Try<Rate> ProcResourceCollector::getCpuUsage()
{
}

void ProcResourceCollector::collectUsage(double& memUsage,
    double& cpuUsage,
    double& timestamp,
    double& duration)
{
  double measuredCpuUsageTotal;
  // Set the initial resource usage on the first reading.
  if (!initialized) {
    prevCpuUsage = 0;
    prevTimestamp = getStartTime(rootPid);
    initialized = true;
  }
  // Read the process stats.
  Try<vector<ProcessStats> > tryProcessTree = getProcessTreeStats();
  if (tryProcessTree.isError()) {
    // TODO(adegtiar): handle the failed Try.
    timestamp = -1;
    return;
  }
  vector<ProcessStats> processTree = tryProcessTree.get();
  timestamp = getCurrentTime();
  // Sum up the resource usage stats.
  aggregateResourceUsage(processTree, memUsage, measuredCpuUsageTotal);
  measuredCpuUsageTotal = ticksToMillis(measuredCpuUsageTotal);
  duration = timestamp - prevTimestamp;
  cpuUsage = measuredCpuUsageTotal - prevCpuUsage;
  // Update the previous usage stats.
  prevTimestamp = timestamp;
  prevCpuUsage = measuredCpuUsageTotal;
}

// TODO(adegtiar): consider doing a full tree walk.
Try<vector<ProcessStats> > ProcResourceCollector::getProcessTreeStats()
{
  vector<ProcessStats> processTree;
  Try<ProcessStats> tryRootStats = getProcessStats(rootPid);
  if (tryRootStats.isError()) {
    return Try<vector<ProcessStats> >::error(tryRootStats.error());
  }
  ProcessStats rootProcess = tryRootStats.get();
  vector<string> allPids = getAllPids();
  // Attempt to add all process in the same tree by checking for:
  //   1) Direct child via match on ppid.
  //   2) Same process group as root.
  //   3) Same session as root.
  foreach (const string& pid, allPids) {
    Try<ProcessStats> tryNextProcess = getProcessStats(pid);
    if (tryNextProcess.isSome()) {
      ProcessStats nextProcess = tryNextProcess.get();
      if (nextProcess.ppid == rootProcess.ppid ||
          nextProcess.pgrp == rootProcess.pgrp ||
          nextProcess.session == rootProcess.session) {
        processTree.push_back(nextProcess);
      }
    } // else process must have died in between calls.
  }
  return processTree;
}

void ProcResourceCollector::aggregateResourceUsage(
    const vector<ProcessStats>& processes,
    double& memTotal,
    double& cpuTotal)
{
  memTotal = 0;
  cpuTotal = 0;
  foreach (const ProcessStats& pinfo, processes) {
    memTotal += pinfo.memUsage;
    cpuTotal += pinfo.cpuTime;
  }
}

}}} // namespace mesos { namespace internal { namespace monitoring {

