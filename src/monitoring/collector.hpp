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

#ifndef __COLLECTOR_HPP__
#define __COLLECTOR_HPP__

namespace mesos { namespace internal { namespace monitoring {

struct Rate {
  Rate(const double _duration, const double _difference)
    : duration(_duration), difference(_difference) {}

  double duration;
  double difference;
};

// TODO(sam): write more doc
// An interface for a module that collects usage/utilization information
// from the operating system. The purpose of this module is to provide an 
// interface for ResourceMonitor to have as a member variable.
class Collector
{
public:
  virtual ~Collector() {}

  virtual double getMemoryUsage() = 0;

  virtual Rate getCpuUsage() = 0;
  
};

}}} // namespace mesos { namespace internal { namespace monitoring {

#endif // __COLLECTOR_HPP__

