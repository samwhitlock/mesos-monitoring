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

#ifndef __PROC_RESOURCE_COLLECTOR_HPP__
#define __PROC_RESOURCE_COLLECTOR_HPP__

#include <list>

#include <sys/types.h>

#include "common/try.hpp"

#include "monitoring/process_resource_collector.hpp"
#include "monitoring/process_stats.hpp"

namespace mesos {
namespace internal {
namespace monitoring {

// An implementation of the ProcessResourceCollector class that
// retrieves resource usage information for a process and all its
// (sub)children from proc.
class ProcResourceCollector : public ProcessResourceCollector
{
public:
  ProcResourceCollector(pid_t rootPid);

  virtual ~ProcResourceCollector();

protected:
  virtual Try<std::list<ProcessStats> > getProcessTreeStats();

  virtual Try<seconds> getStartTime();
};

} // namespace monitoring {
} // namespace internal {
} // namespace mesos {

#endif // __PROC_RESOURCE_COLLECTOR_HPP__
