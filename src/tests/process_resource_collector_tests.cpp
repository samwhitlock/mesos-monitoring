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

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "common/seconds.hpp"
#include "common/try.hpp"

#include "monitoring/process_stats.hpp"
#include "monitoring/process_resource_collector.hpp"

/* Tests for ProcessResourceCollector.
 * TODO(adegtiar): add more tests.
 */

using namespace mesos;
using namespace mesos::internal;
using namespace mesos::internal::monitoring;

using std::list;
using std::string;
using testing::Return;

class MockProcessCollector : public ProcessResourceCollector
{
public:
  MockProcessCollector(pid_t rootPid) : ProcessResourceCollector(rootPid) {}
  MOCK_METHOD0(getProcessTreeStats, Try<list<ProcessStats> >());
  MOCK_METHOD0(getStartTime, Try<seconds>());
};

TEST(ProcessResourceCollectorTest, PropagatesError)
{
  MockProcessCollector collector(1);

  ON_CALL(collector, getStartTime())
    .WillByDefault(Return(seconds(0)));

  // Make sure collectUsage is called before retrieve mem/cpu usage.
  string error_message = "failed query";
  EXPECT_CALL(collector, getProcessTreeStats())
    .WillOnce(Return(Try<list<ProcessStats> >::error(error_message)));

  collector.collectUsage();

  // Make sure the retrieved memory usage propgates the failed query.
  Try<double> mem_usage = collector.getMemoryUsage();
  ASSERT_TRUE(mem_usage.isError());
  ASSERT_EQ(error_message, mem_usage.error());

  // Make sure the retrieved cpu usage propgates the failed query.
  Try<Rate> cpu_usage = collector.getCpuUsage();
  ASSERT_TRUE(cpu_usage.isError());
  ASSERT_EQ(error_message, cpu_usage.error());
}
