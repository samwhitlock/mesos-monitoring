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

#ifndef __PROC_UTILS_HPP__
#define __PROC_UTILS_HPP__

#include <sys/types.h>

#include <list>

#include "common/seconds.hpp"
#include "common/try.hpp"
#include "monitoring/process_stats.hpp"

namespace mesos {
namespace internal {
namespace monitoring {

// Reads from proc and returns a list of all processes running on the
// system.
Try<std::list<pid_t> > getAllPids();

// Retrieves resource usage and metadata for a process. Takes the PID of
// the process to query and returns a ProcessStats struct containing the
// retrieved info.
Try<ProcessStats> getProcessStats(const pid_t& pid);

// Retrieves the system boot time (in time since epoch).
Try<seconds> getBootTime();

// Retrieves the start time (in time since epoch) of the process
// with the given PID.
Try<seconds> getStartTime(const pid_t& pid);

} // namespace monitoring {
} // namespace internal {
} // namespace mesos {

#endif // __PROC_UTILS_HPP__
