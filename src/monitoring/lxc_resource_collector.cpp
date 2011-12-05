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

#include "lxc_resource_collector.hpp"

#include "monitoring/proc_utils.hpp"

#include "common/utils.hpp"
#include "common/resources.hpp"
#include "mesos/mesos.hpp"

namespace mesos { namespace internal { namespace monitoring {

LxcResourceCollector::LxcResourceCollector(const std::string& _containerName)
  : containerName(_containerName), previousTimestamp(-1.0), previousCpuTicks(0.0)
{
}

LxcResourceCollector::~LxcResourceCollector() {}

double LxcResourceCollector::getMemoryUsage()
{
  return getControlGroupDoubleValue("memory.memsw.usage_in_bytes");
}

Rate LxcResourceCollector::getCpuUsage()
{
  if (previousTimestamp == -1.0) {
    previousTimestamp = getContainerStartTime();
  }
  
  double asMillisecs = getCurrentTime();

  double cpuTicks = getControlGroupDoubleValue("cpuacct.usage");

  double elapsedTicks = cpuTicks - previousCpuTicks;
  previousCpuTicks = cpuTicks;
  
  double elapsedTime = asMillisecs - previousTimestamp;
  previousTimestamp = asMillisecs;

  return Rate(elapsedTime, elapsedTicks);
}

bool LxcResourceCollector::getControlGroupValue(
    std::iostream* ios, const std::string& property) const
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

double LxcResourceCollector::getControlGroupDoubleValue(const std::string& property) const
{
  std::stringstream ss;

  getControlGroupValue(&ss, property);
  double d;
  ss >> d;
  return d;
}

double LxcResourceCollector::getContainerStartTime() const
{
  using namespace std;
  vector<string> allPids = getAllPids();//TODO does this need to be sorted?

  return getStartTime(allPids.front());
}

}}} // namespace mesos { namespace internal { namespace monitoring {

