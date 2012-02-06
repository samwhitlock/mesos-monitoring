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

#include <pthread.h>

#include <map>
#include <sstream>
#include <vector>

#include "local.hpp"

#include "common/foreach.hpp"
#include "common/logging.hpp"

#include "configurator/configurator.hpp"

#include "detector/detector.hpp"

#include "master/master.hpp"
#include "master/simple_allocator.hpp"

#include "slave/process_based_isolation_module.hpp"
#include "slave/slave.hpp"

using namespace mesos::internal;

using mesos::internal::master::Allocator;
using mesos::internal::master::Master;
using mesos::internal::master::SimpleAllocator;

using mesos::internal::slave::Slave;
using mesos::internal::slave::IsolationModule;
using mesos::internal::slave::ProcessBasedIsolationModule;

using process::PID;
using process::UPID;

using std::map;
using std::string;
using std::stringstream;
using std::vector;


namespace mesos { namespace internal { namespace local {

static Allocator* allocator = NULL;
static Master* master = NULL;
static map<IsolationModule*, Slave*> slaves;
static MasterDetector* detector = NULL;


void registerOptions(Configurator* configurator)
{
  Logging::registerOptions(configurator);
  Master::registerOptions(configurator);
  Slave::registerOptions(configurator);
  configurator->addOption<int>("num_slaves",
                               "Number of slaves to create for local cluster",
                               1);
}


PID<Master> launch(int numSlaves,
                   int32_t cpus,
                   int64_t mem,
                   bool quiet,
                   Allocator* _allocator)
{
  Configuration conf;
  conf.set("slaves", "*");
  conf.set("num_slaves", numSlaves);
  conf.set("quiet", quiet);

  stringstream out;
  out << "cpus:" << cpus << ";" << "mem:" << mem;
  conf.set("resources", out.str());

  return launch(conf, _allocator);
}


PID<Master> launch(const Configuration& conf, Allocator* _allocator)
{
  int numSlaves = conf.get<int>("num_slaves", 1);
  bool quiet = conf.get<bool>("quiet", false);

  if (master != NULL) {
    fatal("can only launch one local cluster at a time (for now)");
  }

  if (_allocator == NULL) {
    // Create default allocator, save it for deleting later.
    _allocator = allocator = new SimpleAllocator();
  } else {
    // TODO(benh): Figure out the behavior of allocator pointer and remove the
    // else block.
    allocator = NULL;
  }

  master = new Master(_allocator, conf);

  PID<Master> pid = process::spawn(master);

  vector<UPID> pids;

  // TODO(benh): Launching more than one slave is actually not kosher
  // since each slave tries to take the "slave" id.
  for (int i = 0; i < numSlaves; i++) {
    // TODO(benh): Create a local isolation module?
    ProcessBasedIsolationModule *isolationModule =
      new ProcessBasedIsolationModule();
    Slave* slave = new Slave(conf, true, isolationModule);
    slaves[isolationModule] = slave;
    pids.push_back(process::spawn(slave));
  }

  detector = new BasicMasterDetector(pid, pids, true);

  return pid;
}


void shutdown()
{
  if (master != NULL) {
    process::terminate(master->self());
    process::wait(master->self());
    delete master;
    delete allocator;
    master = NULL;

    // TODO(benh): Ugh! Because the isolation module calls back into the
    // slave (not the best design) we can't delete the slave until we
    // have deleted the isolation module. But since the slave calls into
    // the isolation module, we can't delete the isolation module until
    // we have stopped the slave.

    foreachpair (IsolationModule* isolationModule, Slave* slave, slaves) {
      process::terminate(slave->self());
      process::wait(slave->self());
      delete isolationModule;
      delete slave;
    }

    slaves.clear();

    delete detector;
    detector = NULL;
  }
}

}}} // namespace mesos { namespace internal { namespace local {
