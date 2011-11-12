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

#include "utils.hpp"

namespace mesos { namespace internal {


  LxcResourceMonitor::LxcResourceMonitor(const std::string& _containerName)
    : containerName(_containerName)
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
    long memoryInBytes;
    ss >> memoryInBytes;
    ss.str("");//TODO is this necessary?

    //collect cpu usage and do a diff
    if (previousTimestamp == -1) {
      previousTimestamp = getContainerStartTime();
    }
    
    getControlGroupValue(&ss, "cpuacct.usage");
    timeval tv;
    gettimeofday(tv, NULL);
    long asMillisecs = tv.tv_sec * 1000 + (tv.tv_usec / 1000.0) + 0.5;

    long cpuTicks;
    ss >> cpuTicks;
    ss.str("");//TODO is this necessary?

    long elapsedTicks = cpuTicks - previousCpuTicks;
    previousCpuTicks = cpuTicks;
    
    long elapsedTime = asMillisecs - previousTimestamp;
    previousTimestamp = asMillisecs;
  }

  bool LxcResourceMonitor::getControlGroupValue(
      std::iostream* ios, const string& property)
  {
    Try<int> status =
      utils::os::shell(ios, "lxc-cgroup -n %s %s",
                       this->containerName.c_str(), property.c_str());
    
    if (status.isError() || status.get() != 0) {
      LOG(ERROR) << "Failed to get " << property
                 << " for container " << this->containerName;
      //TODO Sam add better log errors like those from
      //setControlGroupValue in lxc_isolation_module
      
      return false;
    }

    return true;
  }

  long LxcResourceMonitor::getContainerStartTime()
  {
    //TODO Sam get the min over the proc directories
  }

}} // namespace mesos { namespace internal {
