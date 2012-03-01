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

#include <process/process.hpp>

#include "common/foreach.hpp"
#include "common/resources.hpp"
#include "common/seconds.hpp"
#include "common/try.hpp"

#include "monitoring/linux/proc_resource_collector.hpp"
#include "monitoring/linux/proc_utils.hpp"

using process::Clock;
using std::ios_base;
using std::list;

namespace mesos {
namespace internal {
namespace monitoring {

inline Try<seconds> initialTryValue()
{
  return Try<seconds>::error("initial value");
}

ProcResourceCollector::ProcResourceCollector(pid_t _rootPid) :
  rootPid(_rootPid),
  isInitialized(false),
  currentMemUsage(Try<double>::error("initial value")),
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
    return Rate(currentTimestamp.get().value - prevTimestamp.get().value,
        currentCpuUsage.get().value - prevCpuUsage.get().value);
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
    currentCpuUsage = Try<seconds>::error(tryProcessTree.error());
    currentTimestamp = Try<seconds>::error(tryProcessTree.error());
    return;
  }
  list<ProcessStats> processTree = tryProcessTree.get();

  // Success, so roll over previous usage.
  prevTimestamp = currentTimestamp;
  prevCpuUsage = currentCpuUsage;

  // Sum up the current resource usage stats.
  double cpuUsageTicks, memUsage;
  aggregateResourceUsage(processTree, memUsage, cpuUsageTicks);
  // TODO(adegtiar): do this via cast?
  currentMemUsage = memUsage;
  currentCpuUsage = seconds(cpuUsageTicks);
  currentTimestamp = seconds(Clock::now());
}

void ProcResourceCollector::updatePreviousUsage()
{
  if (!isInitialized) {
    prevCpuUsage = seconds(0);
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
  Try<list<pid_t> > allPidsTry = getAllPids();
  if (allPidsTry.isError()) {
    return Try<list<ProcessStats> >::error(allPidsTry.error());
  }
  list<pid_t> allPids = allPidsTry.get();
  // Attempt to add all process in the same tree by checking for:
  //   1) Direct child via match on ppid.
  //   2) Same process group as root.
  //   3) Same session as root.
  foreach (pid_t pid, allPids) {
    Try<ProcessStats> tryNextProcess = getProcessStats(pid);
    if (tryNextProcess.isSome()) {
      ProcessStats nextProcess = tryNextProcess.get();
      if (nextProcess.ppid == rootProcess.ppid ||
          nextProcess.pgrp == rootProcess.pgrp ||
          nextProcess.sid == rootProcess.sid) {
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
    cpuTotal += pinfo.cpuTime.value;
  }
}

} // namespace monitoring {
} // namespace internal {
} // namespace mesos {
