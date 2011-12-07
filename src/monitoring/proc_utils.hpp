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

#include <sys/time.h>

#include <list>
#include <string>

#include "common/try.hpp"

namespace mesos {
namespace internal {
namespace monitoring {

struct ProcessStats {
  std::string pid;
  std::string ppid;
  std::string pgrp;
  std::string session;
  double cpuTime;  // Total time in milliseconds.
  double startTime; // Timestamp in milliseconds since epoch.
  double memUsage; // Current RSS usage in bytes.
};

// Reads from proc and returns a list of all processes running on the
// system.
Try<std::list<std::string> > getAllPids();

// Retrieves resource usage and metadata for a process. Takes the PID of
// the process to query and returns a ProcessStats struct containing the
// retrieved info.
Try<ProcessStats> getProcessStats(const std::string& pid);

// Retrieves the system boot time (in milliseconds since epoch).
Try<double> getBootTime();

// Retrieves the start time (in milliseconds since epoch) of the process
// with the given PID.
Try<double> getStartTime(const std::string& pid);

// Retrieves the current system time (in milliseconds since epoch).
inline double getCurrentTime()
{
  timeval ctime;
  gettimeofday(&ctime, NULL);
  return (ctime.tv_sec * 1000.0 + ctime.tv_usec / 1000.0);
}

} // namespace monitoring {
} // namespace internal {
} // namespace mesos {

#endif // __PROC_UTILS_HPP__
