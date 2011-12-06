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

ProcResourceCollector::ProcResourceCollector(const string& _root_pid)
  : root_pid(_root_pid), initialized(false) {}

ProcResourceCollector::~ProcResourceCollector() {}

Try<double> ProcResourceCollector::getMemoryUsage()
{
}

Try<Rate> ProcResourceCollector::getCpuUsage()
{
}

void ProcResourceCollector::collectUsage(double& mem_usage,
    double& cpu_usage,
    double& timestamp,
    double& duration)
{
  double measured_cpu_usage_total;
  // Set the initial resource usage on the first reading.
  if (!initialized) {
    prev_cpu_usage = 0;
    prev_timestamp = getStartTime(root_pid);
    initialized = true;
  }
  // Read the process stats.
  Try<vector<ProcessStats> > try_process_tree = getProcessTreeStats();
  if (try_process_tree.isError()) {
    // TODO(adegtiar): handle the failed Try.
    timestamp = -1;
    return;
  }
  vector<ProcessStats> process_tree = try_process_tree.get();
  timestamp = getCurrentTime();
  // Sum up the resource usage stats.
  aggregateResourceUsage(process_tree, mem_usage, measured_cpu_usage_total);
  measured_cpu_usage_total = ticksToMillis(measured_cpu_usage_total);
  duration = timestamp - prev_timestamp;
  cpu_usage = measured_cpu_usage_total - prev_cpu_usage;
  // Update the previous usage stats.
  prev_timestamp = timestamp;
  prev_cpu_usage = measured_cpu_usage_total;
}

// TODO(adegtiar): consider doing a full tree walk.
Try<vector<ProcessStats> > ProcResourceMonitor::getProcessTreeStats()
{
  vector<ProcessStats> process_tree;
  Try<ProcessStats> tryRootStats = getProcessStats(root_pid);
  if(tryRootStats.isError()) {
    return Try<vector<ProcessStats> >::error(tryRootStats.error());
  }
  ProcessStats root_process = tryRootStats.get();
  vector<string> all_pids = getAllPids();
  // Attempt to add all process in the same tree by checking for:
  //   1) Direct child via match on ppid.
  //   2) Same process group as root.
  //   3) Same session as root.
  foreach (const string& pid, all_pids) {
    Try<ProcessStats> tryNextProcess = getProcessStats(pid);
    if (tryNextProcess.isSome()) {
      ProcessStats nextProcess = tryNextProcess.get();
      if (nextProcess.ppid == root_process.ppid ||
          nextProcess.pgrp == root_process.pgrp ||
          nextProcess.session == root_process.session) {
        process_tree.push_back(nextProcess);
      }
    } // else process must have died in between calls.
  }
  return process_tree;
}

void ProcResourceCollector::aggregateResourceUsage(
    const vector<ProcessStats>& processes,
    double& mem_total,
    double& cpu_total)
{
  mem_total = 0;
  cpu_total = 0;
  foreach (const ProcessStats& pinfo, processes) {
    mem_total += pinfo.memUsage;
    cpu_total += pinfo.cpuTime;
  }
}

UsageReport ProcResourceMonitor::collectUsage()
{
  double mem_usage, cpu_usage, timestamp, duration;
  collectUsage(mem_usage, cpu_usage, timestamp, duration);
  // TODO(adegtiar): do something on failure?
  return generateUsageReport(mem_usage, cpu_usage, timestamp, duration);
}

UsageReport ProcResourceMonitor::generateUsageReport(const double& mem_usage,
    const double& cpu_usage,
    const double& timestamp,
    const double& duration)
{
  Resources resources;
  if (timestamp != -1) {
    // Set CPU usage resources.
    Resource cpu_usage_r;
    cpu_usage_r.set_type(Resource::SCALAR);
    cpu_usage_r.set_name("cpu_usage");
    cpu_usage_r.mutable_scalar()->set_value(cpu_usage);
    resources += cpu_usage_r;
    // Set CPU usage resources.
    Resource mem_usage_r;
    mem_usage_r.set_type(Resource::SCALAR);
    mem_usage_r.set_name("mem_usage");
    mem_usage_r.mutable_scalar()->set_value(mem_usage);
    resources += mem_usage_r;
  }
  // Package into a UsageReport.
  return UsageReport(resources, timestamp, duration);
}

}}} // namespace mesos { namespace internal { namespace monitoring {

