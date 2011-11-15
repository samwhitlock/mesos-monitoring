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

#ifndef __RESOURCE_MONITOR_HPP__
#define __RESOURCE_MONITOR_HPP__

#include "common/resources.hpp"

namespace mesos { namespace internal {

// A single measurement of resources. Some resources may be measured relative
// to a previous measurement, and are therefore associated with a duration.
struct UsageReport {
  UsageReport(Resources _resources,
              const long _timestamp,
              const long _duration)
          : resources(_resources),
            timestamp(_timestamp),
            duration(_duration) {}

  // The collection of resources measured.
  Resources resources;

  // The timestamp of the end of the measurement period (ms since epoch).
  double timestamp;

  // The duration of time the resources are measured over (ms).
  double duration;
};

// An abstract module for collecting resource usage reports for current
// resource utilization.
class ResourceMonitor
{
public:

  virtual ~ResourceMonitor() {}

  // Collects resource usage statistics and returns a UsageReport describing
  // them. For applicable resource, each call reports usage over the time period
  // since the previous invocation. For the first invocation, returns the total
  // usage since the initialization of the resource being monitored.
  virtual UsageReport collectUsage() = 0;
};

}} // namespace mesos { namespace internal {

#endif // __RESOURCE_MONITOR_HPP__

