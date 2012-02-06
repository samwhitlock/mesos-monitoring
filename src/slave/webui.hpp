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

#ifndef __SLAVE_WEBUI_HPP__
#define __SLAVE_WEBUI_HPP__

#ifdef MESOS_WEBUI

#include <process/process.hpp>

#include "slave.hpp"

namespace mesos {
namespace internal {
namespace slave {
namespace webui {

void start(const process::PID<Slave>& slave, const Configuration& conf);

} // namespace webui {
} // namespace slave {
} // namespace internal {
} // namespace mesos {

#endif // MESOS_WEBUI
#endif // __SLAVE_WEBUI_HPP__
