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

#ifndef __LXC_RESOURCE_MONITOR_HPP__
#define __LXC_RESOURCE_MONITOR_HPP__

#include <string>
#include <iostream>

#include "monitoring/resource_monitor.hpp"

namespace mesos { namespace internal { namespace monitoring {

class LxcResourceMonitor : public ResourceMonitor
{
  public:
    LxcResourceMonitor(const std::string& containerName);

    virtual ~LxcResourceMonitor();

    virtual UsageReport collectUsage();
  protected:
    std::string containerName;

    double previousTimestamp;
    double previousCpuTicks;

    double getControlGroupDoubleValue(const std::string& property);

    bool getControlGroupValue(std::iostream* ios, const std::string& property);

    // gets the approximate start time for the container
    // used initial call of collectUsage when no previous data is available
    double getContainerStartTime();
};

}}} // namespace mesos { namespace internal { namespace monitoring {

#endif // __LXC_RESOURCE_MONITOR_HPP__
