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

#include <assert.h>
#include <iostream>
#include <list>
#include <string>

#include "common/foreach.hpp"
#include "common/resources.hpp"
#include "common/try.hpp"
#include "monitoring/proc_resource_collector.hpp"
#include "proc_utils.hpp"

using std::ios_base;
using std::string;
using std::list;

namespace mesos {
namespace internal {
namespace monitoring {

inline Try<double> initialTryValue()
{
  return Try<double>::error("initial value");
}

ProcResourceCollector::ProcResourceCollector(const string& _rootPid) :
  rootPid(_rootPid),
  isInitialized(false),
  currentMemUsage(initialTryValue()),
  currentCpuUsage(initialTryValue()),
  currentTimestamp(initialTryValue()),
  prevCpuUsage(initialTryValue()),
  prevTimestamp(initialTryValue()) {}

ProcResourceCollector::~ProcResourceCollector() {}

Try<double> ProcResourceCollector::getMemoryUsage()
{
  return currentMemUsage;
}

Try<Rate> ProcResourceCollector::getCpuUsage()
{
  if (currentCpuUsage.isSome() && currentTimestamp.isSome() &&
      prevCpuUsage.isSome() && prevTimestamp.isSome()) {
    return Rate(currentTimestamp.get() - prevTimestamp.get(),
        currentCpuUsage.get() - prevCpuUsage.get());
  } else if (prevTimestamp.isError()) {
    // This only happens when process start time lookup fails. Might as
    // well report this first.
    return Try<Rate>::error(prevTimestamp.error());
  } else {
    return Try<Rate>::error(currentCpuUsage.error());
  }
}

void ProcResourceCollector::collectUsage()
{
  updatePreviousUsage();

  // Read the process stats.
  Try<list<ProcessStats> > tryProcessTree = getProcessTreeStats();
  if (tryProcessTree.isError()) {
    currentMemUsage = Try<double>::error(tryProcessTree.error());
    currentCpuUsage = Try<double>::error(tryProcessTree.error());
    currentTimestamp = Try<double>::error(tryProcessTree.error());
    return;
  }
  list<ProcessStats> processTree = tryProcessTree.get();

  // Success, so roll over previous usage.
  prevTimestamp = currentTimestamp;
  prevCpuUsage = currentCpuUsage;

  // Sum up the current resource usage stats.
  double cpuUsageTicks, memUsage;
  aggregateResourceUsage(processTree, memUsage, cpuUsageTicks);
  currentMemUsage = Try<double>::some(memUsage);
  currentCpuUsage = Try<double>::some(cpuUsageTicks);
  currentTimestamp = Try<double>::some(getCurrentTime());
}

void ProcResourceCollector::updatePreviousUsage()
{
  if (!isInitialized) {
    prevCpuUsage = Try<double>::some(0);
    prevTimestamp = getStartTime(rootPid);
    isInitialized = true;
  } else if (currentMemUsage.isSome() && currentCpuUsage.isSome()) {
    // Roll over prev usage from current usage.
    prevCpuUsage = currentCpuUsage;
    prevTimestamp = currentTimestamp;
  } // else keep previous usage.
}

// TODO(adegtiar): consider doing a full tree walk.
Try<list<ProcessStats> > ProcResourceCollector::getProcessTreeStats()
{
  list<ProcessStats> processTree;
  Try<ProcessStats> tryRootStats = getProcessStats(rootPid);
  if (tryRootStats.isError()) {
    return Try<list<ProcessStats> >::error(tryRootStats.error());
  }
  ProcessStats rootProcess = tryRootStats.get();
  Try<list<string> > allPidsTry = getAllPids();
  if (allPidsTry.isError()) {
    return Try<list<ProcessStats> >::error(allPidsTry.error());
  }
  list<string> allPids = allPidsTry.get();
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
    const list<ProcessStats>& processes,
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

} // namespace monitoring {
} // namespace internal {
} // namespace mesos {
