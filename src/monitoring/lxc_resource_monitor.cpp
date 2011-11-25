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

#include <sys/time.h>
#include <vector>

#include "lxc_resource_monitor.hpp"

#include "monitoring/proc_utils.hpp"

#include "common/utils.hpp"
#include "common/resources.hpp"
#include "mesos/mesos.hpp"

namespace mesos { namespace internal { namespace monitoring {

LxcResourceMonitor::LxcResourceMonitor(const std::string& _containerName)
  : previousTimestamp(-1.0), previousCpuTicks(0.0), containerName(_containerName) {}

LxcResourceMonitor::~LxcResourceMonitor() {}

UsageReport LxcResourceMonitor::collectUsage()
{
  if (previousTimestamp == -1) {
    previousTimestamp = getContainerStartTime();
  }
  
  double asMillisecs = getCurrentTime();

  double cpuTicks = getControlGroupDoubleValue("cpuacct.usage");

  double elapsedTicks = cpuTicks - previousCpuTicks;
  previousCpuTicks = cpuTicks;
  
  double elapsedTime = asMillisecs - previousTimestamp;
  previousTimestamp = asMillisecs;

  double memoryInBytes = getControlGroupDoubleValue("memory.memsw.usage_in_bytes");

  LOG(INFO) << "Memory usage in bytes: " << memoryInBytes
            << ", cpu usage: " << elapsedTicks;

  Resource memory;
  memory.set_type(Resource::SCALAR);
  memory.set_name("mem_usage");
  memory.mutable_scalar()->set_value(memoryInBytes);

  Resource cpu;
  cpu.set_type(Resource::SCALAR);
  cpu.set_name("cpu_usage");
  cpu.mutable_scalar()->set_value(elapsedTicks);

  Resources resources;
  resources += cpu;
  resources += memory;

  return UsageReport(resources, asMillisecs, elapsedTime);
}

bool LxcResourceMonitor::getControlGroupValue(
    std::iostream* ios, const std::string& property)
{
  Try<int> status =
    utils::os::shell(ios, "lxc-cgroup -n %s %s",
                     containerName.c_str(), property.c_str());
  
  if (status.isError()) {
    LOG(ERROR) << "Failed to get " << property
               << " for container " << containerName
               << ": " << status.error();
    return false;
  } else if (status.get() != 0) {
    LOG(ERROR) << "Failed to get " << property
               << " for container " << containerName
               << ": lxc-cgroup returned " << status.get();
    return false;
  }

  return true;
}

double LxcResourceMonitor::getControlGroupDoubleValue(
    const std::string& property)
{
  std::stringstream ss;

  getControlGroupValue(&ss, property);
  double d;
  ss >> d;
  return d;
}

double LxcResourceMonitor::getContainerStartTime()
{
  using namespace std;
  vector<string> allPids = getAllPids();//TODO maybe sort this?

  return getStartTime(allPids.front());
}

}}} // namespace mesos { namespace internal { namespace monitoring {

