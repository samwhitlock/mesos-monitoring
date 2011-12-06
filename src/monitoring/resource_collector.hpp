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

#ifndef __RESOURCE_COLLECTOR_HPP__
#define __RESOURCE_COLLECTOR_HPP__

#include "common/try.hpp"

namespace mesos { namespace internal { namespace monitoring {

//TODO(sam): maybe template these for different duration, difference types
struct Rate {
  Rate(const double _duration, const double _difference)
    : duration(_duration), difference(_difference) {}

  double duration;
  double difference;
};

/*
 * An interface for a module that collects usage/utilization information
 * from the operating system. The purpose of this module is to provide an 
 * interface for ResourceMonitor to have as a member variable.
 *
 * Each get method in ResourceCollector will return the appropriate value from the underlying 
 * system. The values are returned are described for each method.
 *
 * For methods that return a rate, the class that implements this interface 
 * will need to keep around the state from the previous call to get that usage 
 * statistic, including the ability to deal with special cases for inital calls.
 */
class ResourceCollector
{
public:
  virtual ~ResourceCollector() {}

  // Returns the number of bytes currently used by the monitored system.
  virtual Try<double> getMemoryUsage() = 0;

  // Returns the milliseconds of CPU time the monitored system has received
  // since it started.
  virtual Try<Rate> getCpuUsage() = 0;
  
};

}}} // namespace mesos { namespace internal { namespace monitoring {

#endif // __RESOURCE_COLLECTOR_HPP__

