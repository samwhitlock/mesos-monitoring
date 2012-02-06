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

#ifndef __NETWORK_HPP__
#define __NETWORK_HPP__

// TODO(benh): Eventually move and associate this code with the
// libprocess protobuf code rather than keep it here.

#include <set>
#include <string>

#include <process/deferred.hpp>
#include <process/executor.hpp>
#include <process/protobuf.hpp>
#include <process/timeout.hpp>

#include "common/foreach.hpp"
#include "common/lambda.hpp"
#include "common/seconds.hpp"
#include "common/utils.hpp"

#include "zookeeper/group.hpp"

// Forward declaration.
class NetworkProcess;

// A "network" is a collection of protobuf processes (may be local
// and/or remote). A network abstracts away the details of maintaining
// which processes are waiting to receive messages and requests in the
// presence of failures and dynamic reconfiguration.
class Network
{
public:
  Network();
  Network(const std::set<process::UPID>& pids);
  virtual ~Network();

  // Adds a PID to this network.
  void add(const process::UPID& pid);

  // Removes a PID from this network.
  void remove(const process::UPID& pid);

  // Set the PIDs that are part of this network.
  void set(const std::set<process::UPID>& pids);

  // Sends a request to each member of the network and returns a set
  // of futures that represent their responses.
  template <typename Req, typename Res>
  process::Future<std::set<process::Future<Res> > > broadcast(
      const Protocol<Req, Res>& protocol,
      const Req& req,
      const std::set<process::UPID>& filter = std::set<process::UPID>());

  // Sends a message to each member of the network.
  template <typename M>
  void broadcast(
      const M& m,
      const std::set<process::UPID>& filter = std::set<process::UPID>());

private:
  // Not copyable, not assignable.
  Network(const Network&);
  Network& operator = (const Network&);

  NetworkProcess* process;
};


class ZooKeeperNetwork : public Network
{
public:
  ZooKeeperNetwork(zookeeper::Group* group);

private:
  // Helper that sets up a watch on the group.
  void watch(const std::set<zookeeper::Group::Membership>& memberships =
             std::set<zookeeper::Group::Membership>());

  // Invoked when the group has updated.
  void ready(const std::set<zookeeper::Group::Membership>& memberships);

  // Invoked if watching the group fails.
  void failed(const std::string& message) const;

  // Invoked if we were unable to watch the group.
  void discarded() const;

  zookeeper::Group* group;

  process::Executor executor;
};


class NetworkProcess : public ProtobufProcess<NetworkProcess>
{
public:
  NetworkProcess() {}

  NetworkProcess(const std::set<process::UPID>& pids)
  {
    set(pids);
  }

  void add(const process::UPID& pid)
  {
    link(pid); // Try and keep a socket open (more efficient).
    pids.insert(pid);
  }

  void remove(const process::UPID& pid)
  {
    // TODO(benh): unlink(pid);
    pids.erase(pid);
  }

  void set(const std::set<process::UPID>& _pids)
  {
    pids.clear();
    foreach (const process::UPID& pid, _pids) {
      add(pid); // Also does a link.
    }
  }

  // Sends a request to each of the groups members and returns a set
  // of futures that represent their responses.
  template <typename Req, typename Res>
  std::set<process::Future<Res> > broadcast(
      const Protocol<Req, Res>& protocol,
      const Req& req,
      const std::set<process::UPID>& filter)
  {
    std::set<process::Future<Res> > futures;
    typename std::set<process::UPID>::const_iterator iterator;
    for (iterator = pids.begin(); iterator != pids.end(); ++iterator) {
      const process::UPID& pid = *iterator;
      if (filter.count(pid) == 0) {
        futures.insert(protocol(pid, req));
      }
    }
    return futures;
  }

  template <typename M>
  void broadcast(
      const M& m,
      const std::set<process::UPID>& filter)
  {
    std::set<process::UPID>::const_iterator iterator;
    for (iterator = pids.begin(); iterator != pids.end(); ++iterator) {
      const process::UPID& pid = *iterator;
      if (filter.count(pid) == 0) {
        process::post(pid, m);
      }
    }
  }

private:
  // Not copyable, not assignable.
  NetworkProcess(const NetworkProcess&);
  NetworkProcess& operator = (const NetworkProcess&);

  std::set<process::UPID> pids;
};


inline Network::Network()
{
  process = new NetworkProcess();
  process::spawn(process);
}


inline Network::Network(const std::set<process::UPID>& pids)
{
  process = new NetworkProcess(pids);
  process::spawn(process);
}


inline Network::~Network()
{
  process::terminate(process);
  process::wait(process);
  delete process;
}


inline void Network::add(const process::UPID& pid)
{
  process::dispatch(process, &NetworkProcess::add, pid);
}


inline void Network::remove(const process::UPID& pid)
{
  process::dispatch(process, &NetworkProcess::remove, pid);
}


inline void Network::set(const std::set<process::UPID>& pids)
{
  process::dispatch(process, &NetworkProcess::set, pids);
}


template <typename Req, typename Res>
process::Future<std::set<process::Future<Res> > > Network::broadcast(
    const Protocol<Req, Res>& protocol,
    const Req& req,
    const std::set<process::UPID>& filter)
{
  return process::dispatch(process, &NetworkProcess::broadcast<Req, Res>,
                           protocol, req, filter);
}


template <typename M>
void Network::broadcast(
    const M& m,
    const std::set<process::UPID>& filter)
{
  // Need to disambiguate overloaded function.
  void (NetworkProcess::*broadcast)(const M&, const std::set<process::UPID>&) =
    &NetworkProcess::broadcast<M>;

  process::dispatch(process, broadcast, m, filter);
}

inline ZooKeeperNetwork::ZooKeeperNetwork(zookeeper::Group* _group)
  : group(_group)
{
  watch();
}


inline void ZooKeeperNetwork::watch(
    const std::set<zookeeper::Group::Membership>& memberships)
{
  process::deferred<void(const std::set<zookeeper::Group::Membership>&)> ready =
    executor.defer(lambda::bind(&ZooKeeperNetwork::ready, this, lambda::_1));

  process::deferred<void(const std::string&)> failed =
    executor.defer(lambda::bind(&ZooKeeperNetwork::failed, this, lambda::_1));

  process::deferred<void(void)> discarded =
    executor.defer(lambda::bind(&ZooKeeperNetwork::discarded, this));

  group->watch(memberships)
    .onReady(ready)
    .onFailed(failed)
    .onDiscarded(discarded);
}


inline void ZooKeeperNetwork::ready(
    const std::set<zookeeper::Group::Membership>& memberships)
{
  LOG(INFO) << "ZooKeeper group memberships changed";

  // Get infos for each membership in order to convert them to PIDs.
  std::set<process::Future<std::string> > futures;

  foreach (const zookeeper::Group::Membership& membership, memberships) {
    futures.insert(group->info(membership));
  }

  std::set<process::UPID> pids;

  process::Timeout timeout = 5.0;

  while (!futures.empty()) {
    process::Future<process::Future<std::string> > future = select(futures);
    if (future.await(timeout.remaining())) {
      CHECK(future.get().isReady());
      process::UPID pid(future.get().get());
      CHECK(pid) << "Failed to parse '" << future.get().get() << "'";
      pids.insert(pid);
      futures.erase(future.get());
    } else {
      watch(); // Try again later assuming empty group.
      return;
    }
  }

  LOG(INFO) << "ZooKeeper group PIDs: "
            << mesos::internal::utils::stringify(pids);

  set(pids); // Update the network.

  watch(memberships);
}


inline void ZooKeeperNetwork::failed(const std::string& message) const
{
  LOG(FATAL) << "Failed to watch ZooKeeper group: "<< message;
}


inline void ZooKeeperNetwork::discarded() const
{
  LOG(FATAL) << "Unexpected discarded future while watching ZooKeeper group";
}

#endif // __NETWORK_HPP__
