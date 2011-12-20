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
#include <list>

#include "common/seconds.hpp"
#include "common/try.hpp"
#include "common/utils.hpp"
#include "monitoring/proc_utils.hpp"

using std::find;
using std::string;
using std::list;

namespace mesos {
namespace internal {
namespace monitoring {

void expectUInt(const string& str)
{
  EXPECT_TRUE(utils::numify<uint64_t>(str).isSome());
}

// A sanity check for a process start time.
void verifyStartTime(const seconds& startTime)
{
  EXPECT_GT(startTime.value, 0.0);
  // Sleep to ensure time difference won't be lost in rounding.
  sleep(1);
  EXPECT_LT(startTime.value, getCurrentTime());
  Try<seconds> bootTime = getBootTime();
  ASSERT_FALSE(bootTime.isError());
  EXPECT_GT(startTime.value, getBootTime().get().value);
}

TEST(ProcUtilsTest, BootTime)
{
  Try<seconds> bootTime = getBootTime();
  ASSERT_FALSE(bootTime.isError());
  EXPECT_GT(bootTime.get().value, 0.0);
}

TEST(ProcUtilsTest, CurrentTime)
{
  EXPECT_GT(getCurrentTime(), 0.0);
  Try<seconds> bootTime = getBootTime();
  ASSERT_FALSE(bootTime.isError());
  EXPECT_GT(getCurrentTime(), bootTime.get().value);
}

TEST(ProcUtilsTest, StartTime)
{
  Try<seconds> startTime = getStartTime("self");
  ASSERT_FALSE(startTime.isError());
  verifyStartTime(startTime.get());
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
  verifyStartTime(processStats.startTime);
  EXPECT_GT(processStats.cpuTime.value, 0.0);
  EXPECT_GT(processStats.memUsage, 0.0);
}

TEST(ProcUtilsTest, GetAllPids)
{
  Try<ProcessStats> tryProcessStats = getProcessStats("self");
  ASSERT_FALSE(tryProcessStats.isError());
  string mPid = tryProcessStats.get().pid;

  Try<list<string> > allPidsTry = getAllPids();
  ASSERT_FALSE(allPidsTry.isError());
  list<string> allPids = allPidsTry.get();
  ASSERT_FALSE(allPids.empty());
  // Make sure the list contains the pid of the current process.
  EXPECT_NE(find(allPids.begin(), allPids.end(), mPid), allPids.end());
  // Make sure all pids are natural numbers.
  foreach (const string& pid, allPids) {
    expectUInt(pid);
  }
}

} // namespace monitoring {
} // namespace internal {
} // namespace mesos {
