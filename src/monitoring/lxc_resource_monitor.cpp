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

#include <ctime>

#include "lxc_resource_monitor.hpp"

#include "common/utils.hpp"
#include "common/resources.hpp"
#include "mesos/mesos.hpp"

namespace mesos { namespace internal { namespace monitoring {

inline double toMillisecs(timeval tv)
{
  return tv.tv_sec * 1000.0 + (tv.tv_usec / 1000.0) + 0.5;
}

LxcResourceMonitor::LxcResourceMonitor(const std::string& _containerName)
  : previousTimestamp(-1.0), previousCpuTicks(0.0), containerName(_containerName)
{
}

LxcResourceMonitor::~LxcResourceMonitor()
{
}

UsageReport LxcResourceMonitor::collectUsage()
{
  //collect memory usage
  std::stringstream ss;
  getControlGroupValue(&ss, "memory.memsw.usage_in_bytes");
  double memoryInBytes;
  ss >> memoryInBytes;

  //collect cpu usage and do a diff
  if (previousTimestamp == -1) {
    previousTimestamp = getContainerStartTime();
  }
  
  getControlGroupValue(&ss, "cpuacct.usage");
  timeval tv;
  gettimeofday(tv, NULL);
  double asMillisecs = toMillisecs(tv);

  double cpuTicks;
  ss >> cpuTicks;

  double elapsedTicks = cpuTicks - previousCpuTicks;
  previousCpuTicks = cpuTicks;
  
  double elapsedTime = asMillisecs - previousTimestamp;
  previousTimestamp = asMillisecs;

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
    std::iostream* ios, const string& property)
{
  Try<int> status =
    utils::os::shell(ios, "lxc-cgroup -n %s %s",
                     this->containerName.c_str(), property.c_str());
  
  if (status.isError()) {
    LOG(ERROR) << "Failed to get " << property
               << " for container " << container
               << ": " << status.error();
    return false;
  } else if (status.get() != 0) {
    LOG(ERROR) << "Failed to get " << property
               << " for container " << container
               << ": lxc-cgroup returned " << status.get();
    return false;
  }

  return true;
}

double LxcResourceMonitor::getContainerStartTime()
{
}

}}} // namespace mesos { namespace internal { namespace monitoring {

