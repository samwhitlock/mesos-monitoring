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

#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "common/attributes.hpp"

using namespace mesos;
using namespace mesos::internal;

using std::string;


TEST(AttributesTest, Parsing)
{
  Attributes a = Attributes::parse("cpus:45.55;"
                                    "ports:[10000-20000, 30000-50000];"
                                    "rack:rack1,rack2");
  ASSERT_EQ(Value::SCALAR, a.get(0).type());
  ASSERT_EQ("cpus", a.get(0).name());
  ASSERT_EQ(45.55, a.get(0).scalar().value());

  ASSERT_EQ(Value::RANGES, a.get(1).type());
  ASSERT_EQ("ports", a.get(1).name());
  ASSERT_EQ(2, a.get(1).ranges().range_size());
  ASSERT_EQ(10000, a.get(1).ranges().range(0).begin());
  ASSERT_EQ(20000, a.get(1).ranges().range(0).end());
  ASSERT_EQ(30000, a.get(1).ranges().range(1).begin());
  ASSERT_EQ(50000, a.get(1).ranges().range(1).end());


  ASSERT_EQ(Value::TEXT, a.get(2).type());
  ASSERT_EQ("rack", a.get(2).name());
  ASSERT_EQ("rack1,rack2", a.get(2).text().value());
}