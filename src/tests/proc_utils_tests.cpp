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

#include <algorithm>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "common/utils.hpp"
#include "common/try.hpp"
#include "monitoring/proc_utils.hpp"

using std::find;
using std::string;
using std::vector;

namespace mesos {
namespace internal {
namespace monitoring {

void expectUInt(const string& str)
{
  EXPECT_TRUE(utils::numify<uint64_t>(str).isSome());
}

// A sanity check for a process start time.
void verifyStartTime(const double& startTime)
{
  EXPECT_GT(startTime, 0.0);
  // TODO(adegtiar): fix the use of Trys.
  EXPECT_GT(startTime, getBootTime().get());
  EXPECT_LT(startTime, getCurrentTime());
}

TEST(DISABLED_ProcUtilsTest, EnableOnLinuxOnly) {}

TEST(ProcUtilsTest, BootTime)
{
  // TODO(adegtiar): fix the use of Trys.
  EXPECT_GT(getBootTime().get(), 0.0);
}

TEST(ProcUtilsTest, CurrentTime)
{
  EXPECT_GT(getCurrentTime(), 0.0);
  // TODO(adegtiar): fix the use of Trys.
  EXPECT_GT(getCurrentTime(), getBootTime().get());
}

TEST(ProcUtilsTest, StartTime)
{
  double startTime = getStartTime("self");
  verifyStartTime(startTime);
}

TEST(ProcUtilsTest, ProcessStats)
{
  Try<ProcessStats> tryProcessStats = getProcessStats("self");
  ASSERT_FALSE(tryProcessStats.isError());
  ProcessStats processStats = tryProcessStats.get();
  expectUInt(processStats.pid);
  expectUInt(processStats.ppid);
  expectUInt(processStats.pgrp);
  expectUInt(processStats.session);
  verifyStartTime(bootJiffiesToMillis(processStats.startTime));
  EXPECT_GT(processStats.cpuTime, 0.0);
  EXPECT_GT(processStats.memUsage, 0.0);
}

TEST(ProcUtilsTest, GetAllPids)
{
  Try<ProcessStats> tryProcessStats = getProcessStats("self");
  ASSERT_FALSE(tryProcessStats.isError());
  string mPid = tryProcessStats.get().pid;
  vector<string> allPids = getAllPids();
  ASSERT_FALSE(allPids.empty());
  // Make sure the list contains the pid of the current process.
  EXPECT_NE(find(allPids.begin(), allPids.end(), mPid), allPids.end());
  // Make sure all pids are natural numbers.
  foreach(const string& pid, allPids) {
    expectUInt(pid);
  }
}

} // namespace monitoring {
} // namespace internal {
} // namespace mesos {
