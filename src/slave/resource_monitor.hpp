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

#include <process/process.hpp>
#include <process/future.hpp>

#include "messages/messages.hpp"

#include "monitoring/resource_collector.hpp"

using mesos::internal::monitoring::ResourceCollector;

namespace mesos {
namespace internal {
namespace slave {

// An abstract module for collecting resource usage reports for current
// resource utilization.

class ResourceMonitor : public process::Process<ResourceMonitor>
{
public:
  ResourceMonitor(ResourceCollector* collector);

  virtual ~ResourceMonitor();

  // Collects resource usage statistics and returns a UsageReport describing
  // them. For applicable resource, each call reports usage over the time period
  // since the previous invocation. For the first invocation, returns the total
  // usage since the initialization of the resource being monitored.
  virtual process::Future<UsageMessage> collectUsage(const FrameworkID& frameworkId,
                                                     const ExecutorID& executorId);

protected:
  ResourceCollector* collector;
};

} // namespace slave {
} // namespace internal {
} // namespace mesos {

#endif // __RESOURCE_MONITOR_HPP__
