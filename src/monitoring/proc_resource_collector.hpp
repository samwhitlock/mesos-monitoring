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

#ifndef __PROC_RESOURCE_COLLECTOR_HPP__
#define __PROC_RESOURCE_COLLECTOR_HPP__

#include <iostream>
#include <string>
#include <vector>

#include "monitoring/process_resource_collector.hpp"
#include "proc_utils.hpp"

namespace mesos {
namespace internal {
namespace monitoring {

// An implementation of the ProcessResourceCollector class that retrieves
// resource usage information for a process and all its (sub)children from
// proc.
class ProcResourceCollector : public ProcessResourceCollector
{
public:

  ProcResourceCollector(const std::string& root_pid);

  virtual ~ProcResourceCollector();

  virtual Try<double> getMemoryUsage();

  virtual Try<Rate> getCpuUsage();

private:

  const std::string root_pid;
  double prev_cpu_usage;
  double prev_timestamp;
  bool initialized;

  // Retrieve the info for all processes rooted at the process with the given
  // PID.
 Try<std::vector<ProcessStats> > getProcessTreeStats();

  // Aggregates the info all of the given ProcessStats and stores the result in
  // mem_total and cpu_total.
  void aggregateResourceUsage(const std::vector<ProcessStats>& processes,
      double& mem_total,
      double& cpu_total);

  // Collects resource usage statistics and populates the arguments describing
  // them.
  void collectUsage(double& mem_usage,
    double& cpu_usage,
    double& timestamp,
    double& duration);
};

} // namespace monitoring {
} // namespace internal {
} // namespace mesos {

#endif // __PROC_RESOURCE_COLLECTOR_HPP__

