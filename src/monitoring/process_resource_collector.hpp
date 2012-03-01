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

#ifndef __PROCESS_RESOURCE_COLLECTOR_HPP__
#define __PROCESS_RESOURCE_COLLECTOR_HPP__

#include <list>

#include <sys/types.h>

#include "common/seconds.hpp"
#include "common/try.hpp"

#include "monitoring/process_stats.hpp"
#include "monitoring/resource_collector.hpp"

namespace mesos {
namespace internal {
namespace monitoring {

// An abstract implementation of the ResourceCollector class that
// retrieves resource usage information for a process or processes.
class ProcessResourceCollector : public ResourceCollector
{
public:

  // Creates a new ProcessResourceCollector appropriate for the current
  // system. If no monitor can be constructed, returns NULL.
  static ProcessResourceCollector* create(pid_t rootPid);

  ProcessResourceCollector(pid_t rootPid);

  virtual ~ProcessResourceCollector() {}

  virtual void collectUsage();

  virtual Try<double> getMemoryUsage();

  virtual Try<Rate> getCpuUsage();

protected:
  const pid_t rootPid;

  // Retrieve the info for all processes rooted at the process with the
  // given PID.
 virtual Try<std::list<ProcessStats> > getProcessTreeStats() = 0;

 // Retrieve the start time of the monitored process.
 virtual Try<seconds> getStartTime() = 0;

private:
  Try<double> currentMemUsage;

  Try<seconds> prevCpuUsage;

  Try<seconds> currentCpuUsage;

  Try<seconds> prevTimestamp;

  Try<seconds> currentTimestamp;

  bool isInitialized;

 // Updates or initializes the previous resource usage state.
 void updatePreviousUsage();

  // Aggregates the info all of the given ProcessStats and stores the result in
  // memTotal and cpuTotal.
  void aggregateResourceUsage(const std::list<ProcessStats>& processes,
      double& memTotal,
      double& cpuTotal);
};

} // namespace monitoring {
} // namespace internal {
} // namespace mesos {

#endif // __PROCESS_RESOURCE_COLLECTOR_HPP__
