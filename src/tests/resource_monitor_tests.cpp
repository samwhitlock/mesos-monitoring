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

#include "common/try.hpp"
#include "common/resources.hpp"
#include "monitoring/resource_collector.hpp"
#include "monitoring/resource_monitor.hpp"

/* Tests for resource monitor
 * - just one test to make sure that it pushes through state correctly
 */ 

using namespace mesos;
using namespace mesos::internal;
using namespace mesos::internal::monitoring;

using testing::Return;
using process::Clock;

class MockCollector : public ResourceCollector
{
public:
  // TODO(sam): Fix/confirm change.
  MOCK_METHOD0(getMemoryUsage, Try<double>());
  MOCK_METHOD0(getCpuUsage, Try<Rate>());
};

TEST(ResourceMonitorTest, MonitorsCorrectly)
{
  MockCollector* mc = new MockCollector();

  // TODO(sam) put call requirements here!
  double memoryUsage = 123456789.0;
  EXPECT_CALL(*mc, getMemoryUsage())
    .Times(1)
    .WillOnce(Return(Try<double>::some(memoryUsage)));

  double duration = 13579.0, difference = 2468.0;
  EXPECT_CALL(*mc, getCpuUsage())
    .Times(1)
    .WillOnce(Return(Try<Rate>::some(Rate(duration, difference))));

  ResourceMonitor rm(mc);

  Try<UsageReport> tur = rm.collectUsage();

  ASSERT_TRUE(tur.isSome());
  
  UsageReport ur = tur.get();
  Resources r = ur.resources;

  EXPECT_EQ(duration, ur.duration);
  EXPECT_FALSE(ur.timestamp > Clock::now());//To fix roundoff errors

  EXPECT_EQ(memoryUsage, r.get("mem_usage", Resource::Scalar()).value());
  EXPECT_EQ(difference, r.get("cpu_usage", Resource::Scalar()).value());
}
