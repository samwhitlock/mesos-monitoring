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

#include <list>

#include <sys/types.h>

#include "common/foreach.hpp"
#include "common/try.hpp"

#include "monitoring/linux/proc_resource_collector.hpp"
#include "monitoring/linux/proc_utils.hpp"

#include "monitoring/process_stats.hpp"

using std::list;

namespace mesos {
namespace internal {
namespace monitoring {

ProcResourceCollector::ProcResourceCollector(pid_t _rootPid) :
  ProcessResourceCollector(_rootPid) {}

ProcResourceCollector::~ProcResourceCollector() {}

// TODO(adegtiar): consider doing a full tree walk.
Try<list<ProcessStats> > ProcResourceCollector::getProcessTreeStats()
{
  list<ProcessStats> processTree;
  Try<ProcessStats> tryRootStats = getProcessStats(rootPid);
  if (tryRootStats.isError()) {
    return Try<list<ProcessStats> >::error(tryRootStats.error());
  }
  ProcessStats rootProcess = tryRootStats.get();
  Try<list<pid_t> > allPidsTry = getAllPids();
  if (allPidsTry.isError()) {
    return Try<list<ProcessStats> >::error(allPidsTry.error());
  }
  list<pid_t> allPids = allPidsTry.get();
  // Attempt to add all process in the same tree by checking for:
  //   1) Direct child via match on ppid.
  //   2) Same process group as root.
  //   3) Same session as root.
  foreach (pid_t pid, allPids) {
    Try<ProcessStats> tryNextProcess = getProcessStats(pid);
    if (tryNextProcess.isSome()) {
      ProcessStats nextProcess = tryNextProcess.get();
      if (nextProcess.ppid == rootProcess.ppid ||
          nextProcess.pgrp == rootProcess.pgrp ||
          nextProcess.sid == rootProcess.sid) {
        processTree.push_back(nextProcess);
      }
    } // else process must have died in between calls.
  }
  return processTree;
}

Try<seconds> ProcResourceCollector::getStartTime()
{
  return monitoring::getStartTime(rootPid);
}

} // namespace monitoring {
} // namespace internal {
} // namespace mesos {
