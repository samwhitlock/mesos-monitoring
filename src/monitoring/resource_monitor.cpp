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

#include "proc_utils.hpp"

namespace mesos { namespace internal { namespace monitoring {

  ResourceMonitor::ResourceMonitor(const Collector& _collector)
    : collector(_collector) {}

  // The default implementation collects only the cpu and memory
  // for use in creating a UsageMessage
  UsageReport ResourceMonitor::collectUsage()
  {
    double now = getCurrentTime();

    Resource memory;
    memory.set_type(Resource::SCALAR);
    memory.set_name("mem_usage");
    memory.mutable_scalar()->set_value(collector.getMemoryUsage());

    Rate cpu = collector.getCpuUsage();

    Resource cpu;
    cpu.set_type(Resource::SCALAR);
    cpu.set_name("cpu_usage");
    cpu.mutable_scalar()->set_value(cpu.difference);

    Resources resources;
    resources += cpu;
    resources += memory;

    return UsageReport(resources, now, cpu.duration);
  }

}}} // namespace mesos { namespace internal { namespace monitoring {
