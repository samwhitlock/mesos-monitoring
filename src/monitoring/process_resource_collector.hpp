/**
 * licensed to the apache software foundation (asf) under one
 * or more contributor license agreements.  see the notice file
 * distributed with this work for additional information
 * regarding copyright ownership.  the asf licenses this file
 * to you under the apache license, version 2.0 (the
 * "license"); you may not use this file except in compliance
 * with the license.  you may obtain a copy of the license at
 *
 *     http://www.apache.org/licenses/license-2.0
 *
 * unless required by applicable law or agreed to in writing, software
 * distributed under the license is distributed on an "as is" basis,
 * without warranties or conditions of any kind, either express or implied.
 * see the license for the specific language governing permissions and
 * limitations under the license.
 */

#ifndef __PROCESS_RESOURCE_COLLECTOR_HPP__
#define __PROCESS_RESOURCE_COLLECTOR_HPP__

#include <sys/types.h>

#include "monitoring/resource_collector.hpp"

namespace mesos {
namespace internal {
namespace monitoring {

// An abstract implementation of the ResourceCollector class that
// retrieves resource usage information for a process or processes.
class ProcessResourceCollector : public ResourceCollector
{
public:

  // Creates a new ProcessResourceCollector appropriate for the current
  // system. If no monitor can be constructed, returns NULL.
  static ProcessResourceCollector* create(pid_t rootPid);

  virtual ~ProcessResourceCollector() {}
};

} // namespace monitoring {
} // namespace internal {
} // namespace mesos {

#endif // __PROCESS_RESOURCE_COLLECTOR_HPP__
