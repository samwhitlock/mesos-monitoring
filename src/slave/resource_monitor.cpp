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

#include "resource_monitor.hpp"
#include <mesos/mesos.hpp>
#include "common/resources.hpp"//TODO(sam) do I even need this?

using process::Clock;
using process::Future;
using process::Promise;
using mesos::internal::monitoring::Rate;

namespace mesos {
namespace internal {
namespace slave {

ResourceMonitor::ResourceMonitor(ResourceCollector* _collector)
  : collector(_collector) {}

ResourceMonitor::~ResourceMonitor()
{
  delete collector;
}

// The default implementation collects only the cpu and memory
// for use in creating a UsageMessage
Future<UsageMessage> ResourceMonitor::collectUsage(const FrameworkID& frameworkId,
                                                   const ExecutorID& executorId)
{
  Promise<UsageMessage> p;
  Resources resources;
  double now = Clock::now();
  double duration = 0;

  collector->collectUsage();
  // TODO(adegtiar or sam): consider making this more general to
  // avoid code duplication and make it more flexible, e.g.
  // foreach usageType in ["mem_usage", "cpu_usage", ...]
  //   collector->getUsage(usageType); (+ Try stuff, etc)
  Try<double> memUsage = collector->getMemoryUsage();
  if (memUsage.isSome()) {
    ::mesos::Resource memory;
    memory.set_type(Value::SCALAR);
    memory.set_name("mem_usage");
    memory.mutable_scalar()->set_value(memUsage.get());
    resources += memory;
  } else {
    p.fail(memUsage.error());
    return p.future();
  }

  Try<Rate> cpuUsage = collector->getCpuUsage();
  if (cpuUsage.isSome()) {
    Rate rate = cpuUsage.get();
    ::mesos::Resource cpu;
    cpu.set_type(Value::SCALAR);
    cpu.set_name("cpu_usage");
    cpu.mutable_scalar()->set_value(rate.difference);
    duration = rate.duration;
    resources += cpu;
  } else {
    p.fail(cpuUsage.error());
    return p.future();
  }

  // TODO(adegtiar or sam): Consider returning partial usage reports.
  // For now if one fails, the other will almost certainly fail, and
  // so may not be worthwhile. This could change.
  // return UsageReport(resources, now, duration);

  // Assemble the UsageMessage and return the corresponding Future.
  UsageMessage usage;
  usage.mutable_framework_id()->MergeFrom(frameworkId);
  usage.mutable_executor_id()->MergeFrom(executorId);
  usage.mutable_resources()->MergeFrom(resources);
  usage.set_timestamp(now);
  usage.set_duration(duration);

  // Cast into a Future<UsageMessage> and return.
  return usage;
}

} // namespace slave {
} // namespace internal {
} // namespace mesos {
