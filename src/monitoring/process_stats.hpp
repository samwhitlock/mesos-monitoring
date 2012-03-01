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

#ifndef __PROCESS_STATS_HPP__
#define __PROCESS_STATS_HPP__

#include <sys/types.h>

namespace mesos {
namespace internal {
namespace monitoring {

// Contains statistics about a running process.
struct ProcessStats
{
  ProcessStats(pid_t _pid, pid_t _ppid, pid_t _pgrp, pid_t _sid,
      seconds _cpuTime, seconds _startTime, double _memUsage) :
    pid(_pid), ppid(_ppid), pgrp(_pgrp), sid(_sid), cpuTime(_cpuTime),
    startTime(_startTime), memUsage(_memUsage) {}

  const pid_t pid;
  const pid_t ppid;
  const pid_t pgrp;
  const pid_t sid;
  const seconds cpuTime;   // Total cpu time used.
  const seconds startTime; // Timestamp as time elapsed since epoch.
  const double memUsage;   // Current RSS usage in bytes.
};

} // namespace monitoring {
} // namespace internal {
} // namespace mesos {

#endif // __PROCESS_STATS_HPP__
