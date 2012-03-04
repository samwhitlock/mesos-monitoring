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

#include <list>

#include <sys/time.h>

#include <process/process.hpp>

#include "monitoring/linux/proc_utils.hpp"
#include "monitoring/linux/lxc_resource_collector.hpp"

#include "common/utils.hpp"
#include "common/resources.hpp"
#include "common/seconds.hpp"
#include "mesos/mesos.hpp"

using process::Clock;

namespace mesos {
namespace internal {
namespace monitoring {

LxcResourceCollector::LxcResourceCollector(const std::string& _containerName)
  : containerName(_containerName), previousTimestamp(-1.0), previousCpuTicks(0.0)
{
}

LxcResourceCollector::~LxcResourceCollector() {}

Try<double> LxcResourceCollector::getMemoryUsage()
{
  return getControlGroupDoubleValue("memory.memsw.usage_in_bytes");
}

Try<Rate> LxcResourceCollector::getCpuUsage()
{
  if (previousTimestamp == -1.0) {
    // TODO(sam): Make this handle the Try of the getStartTime.
    previousTimestamp = getContainerStartTime().get().value;
  }

  double seconds = Clock::now();

  Try<double> cpuTicks = getControlGroupDoubleValue("cpuacct.usage");

  if (cpuTicks.isError()) {
    return Try<Rate>::error("unable to read cpuacct.usage from lxc");
  }

  double ticks = nanoseconds(cpuTicks.get()).secs();
  double elapsedTicks = ticks - previousCpuTicks;
  previousCpuTicks = ticks;

  double elapsedTime = seconds - previousTimestamp;
  previousTimestamp = seconds;

  return Rate(elapsedTime, elapsedTicks);
}

bool LxcResourceCollector::getControlGroupValue(
    std::iostream* ios, const std::string& property) const
{
  Try<int> status =
    utils::os::shell(ios, "lxc-cgroup -n %s %s",
                     containerName.c_str(), property.c_str());

  if (status.isError()) {
    LOG(INFO) << "Failed to get " << property
               << " for container " << containerName
               << ": " << status.error();
    return false;
  } else if (status.get() != 0) {
    LOG(INFO) << "Failed to get " << property
               << " for container " << containerName
               << ": lxc-cgroup returned " << status.get();
    return false;
  }

  return true;
}

Try<double> LxcResourceCollector::getControlGroupDoubleValue(const std::string& property) const
{
  std::stringstream ss;

  if (getControlGroupValue(&ss, property)) {
    double d;
    ss >> d;
    return d;
  } else {
    return Try<double>::error("unable to read control group double value");
  }
}

Try<seconds> LxcResourceCollector::getContainerStartTime() const
{
  using namespace std;
  Try<list<pid_t> > allPidsTry = getAllPids();
  if (allPidsTry.isError()) {
    return Try<seconds>::error(allPidsTry.error());
  }
  // TODO does this need to be sorted?
  return getStartTime(allPidsTry.get().front());
}

} // namespace monitoring {
} // namespace internal {
} // namespace mesos {
