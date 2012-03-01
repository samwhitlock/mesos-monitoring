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

#include <process/process.hpp>
#include <process/future.hpp>

#include "common/try.hpp"
#include "common/resources.hpp"
#include "monitoring/resource_collector.hpp"
#include "slave/resource_monitor.hpp"

/* Tests for resource monitor
 * - just one test to make sure that it pushes through state correctly
 */ 

using namespace mesos;
using namespace mesos::internal;
using namespace mesos::internal::monitoring;
using namespace mesos::internal::slave;

using process::Clock;
using process::Future;
using std::string;
using testing::Expectation;
using testing::Return;

class MockCollector : public ResourceCollector
{
public:
  // TODO(sam): Fix/confirm change.
  MOCK_METHOD0(getMemoryUsage, Try<double>());
  MOCK_METHOD0(getCpuUsage, Try<Rate>());
  MOCK_METHOD0(collectUsage, void());
};

TEST(ResourceMonitorTest, MonitorsCorrectly)
{
  MockCollector* mock_collector = new MockCollector();

  // TODO(sam) put call requirements here!

  // Make sure collectUsage is called before retrieve mem/cpu usage.
  Expectation collect_usage = EXPECT_CALL(*mock_collector, collectUsage())
    .Times(1);

  // Set the memory usage the collector will return.
  double memoryUsage = 123456789.0;
  EXPECT_CALL(*mock_collector, getMemoryUsage())
    .After(collect_usage)
    .WillOnce(Return(Try<double>::some(memoryUsage)));

  // Set the cpu usage the collector will return.
  double duration = 13579.0, difference = 2468.0;
  EXPECT_CALL(*mock_collector, getCpuUsage())
    .After(collect_usage)
    .WillOnce(Return(Try<Rate>::some(Rate(duration, difference))));

  ResourceMonitor mocked_monitor(mock_collector);

  // Create fake IDs.
  FrameworkID framework_id;
  framework_id.set_value("framework_id1");
  ExecutorID executor_id;
  executor_id.set_value("executor_id1");

  // Collect the usage.
  Future<UsageMessage> usage_msg_future = mocked_monitor.collectUsage(
      framework_id, executor_id);

  // Setting a timeout in case it gets stuck.
  usage_msg_future.await(5);

  ASSERT_TRUE(usage_msg_future.isReady());
  
  UsageMessage usage_msg = usage_msg_future.get();

  // Make sure the returned UsageMessage matches the expected values.
  EXPECT_EQ(duration, usage_msg.duration());
  EXPECT_FALSE(usage_msg.timestamp() > Clock::now()); // To fix roundoff errors.
  
  // TODO(adegtiar or sam): fix this check.
  //EXPECT_EQ(framework_id, usage_msg.framework_id());
  //EXPECT_EQ(executor_id, usage_msg.executor_id());

  Resources usage = usage_msg.resources();
  EXPECT_EQ(memoryUsage, usage.get("mem_usage", Value::Scalar()).value());
  EXPECT_EQ(difference, usage.get("cpu_usage", Value::Scalar()).value());
}
