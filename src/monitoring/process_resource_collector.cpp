/**
 * licensed to the apache software foundation (asf) under one
 * or more contributor license agreements.  see the notice file
 * distributed with this work for additional information
 * regarding copyright ownership.  the asf licenses this file
 * to you under the apache license, version 2.0 (the
 * "license"); you may not use this file except in compliance
 * with the license.  you may obtain a copy of the license at
 *
 *     http://www.apache.org/licenses/license-2.0
 *
 * unless required by applicable law or agreed to in writing, software
 * distributed under the license is distributed on an "as is" basis,
 * without warranties or conditions of any kind, either express or implied.
 * see the license for the specific language governing permissions and
 * limitations under the license.
 */

#include <list>

#include <sys/types.h>

#include <process/process.hpp>

#include "common/foreach.hpp"
#include "common/seconds.hpp"
#include "common/try.hpp"

#ifdef __linux__
#include "monitoring/linux/proc_resource_collector.hpp"
#endif

#include "monitoring/process_resource_collector.hpp"

using process::Clock;
using std::list;

namespace mesos {
namespace internal {
namespace monitoring {

ProcessResourceCollector* ProcessResourceCollector::create(pid_t rootPid)
{
#ifdef __linux__
  return new ProcResourceCollector(rootPid);
#else
  return NULL;
#endif
}

inline Try<seconds> initialTryValue()
{
  return Try<seconds>::error("initial value");
}

ProcessResourceCollector::ProcessResourceCollector(pid_t _rootPid) :
  rootPid(_rootPid),
  isInitialized(false),
  currentMemUsage(Try<double>::error("initial value")),
  currentCpuUsage(initialTryValue()),
  currentTimestamp(initialTryValue()),
  prevCpuUsage(initialTryValue()),
  prevTimestamp(initialTryValue()) {}

Try<double> ProcessResourceCollector::getMemoryUsage()
{
  return currentMemUsage;
}

Try<Rate> ProcessResourceCollector::getCpuUsage()
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

void ProcessResourceCollector::collectUsage()
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

  // Update current usage.
  currentMemUsage = memUsage;
  currentCpuUsage = seconds(cpuUsageTicks);
  currentTimestamp = seconds(Clock::now());
}

void ProcessResourceCollector::updatePreviousUsage()
{
  if (!isInitialized) {
    prevCpuUsage = seconds(0);
    prevTimestamp = getStartTime();
    isInitialized = true;
  } else if (currentMemUsage.isSome() && currentCpuUsage.isSome()) {
    // Roll over prev usage from current usage.
    prevCpuUsage = currentCpuUsage;
    prevTimestamp = currentTimestamp;
  } // else keep previous usage.
}

void ProcessResourceCollector::aggregateResourceUsage(
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
