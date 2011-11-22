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

#ifndef __PROCESS_RESOURCE_MONITOR_HPP__
#define __PROCESS_RESOURCE_MONITOR_HPP__

#include <string>

#include "monitoring/resource_monitor.hpp"

namespace mesos { namespace internal { namespace monitoring {

// An abstract implementation of the ResourceMonitor class that
// retrieves resource usage information for a process or processes.
class ProcessResourceMonitor: public ResourceMonitor
{
public:

  // Creates a new ProcessResourceMonitor appropriate for the current
  // system. If no monitor can be constructed, returns NULL.
  static ProcessResourceMonitor* create(const std::string& root_pid);
};

}}} // namespace mesos { namespace internal { namespace monitoring {

#endif // __PROCESS_RESOURCE_MONITOR_HPP__
