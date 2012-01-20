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

#include <assert.h>

#include <glog/logging.h>

#include <iostream>
#include <map>

#include <boost/tuple/tuple.hpp>

#include <process/dispatch.hpp>
#include <process/process.hpp>

#include "common/fatal.hpp"

#include "zookeeper/zookeeper.hpp"

// DO NOT REMOVE! Removing this will require also changing which
// ZooKeeper library get's used for linking, right now the Makefile is
// assuming the multithreaded library will get used ...
#define USE_THREADED_ZOOKEEPER

using boost::cref;
using boost::tuple;

using process::Future;
using process::PID;
using process::Process;
using process::Promise;

using std::cerr;
using std::cout;
using std::endl;
using std::map;
using std::string;
using std::vector;


// Singleton instance of WatcherProcessManager.
class WatcherProcessManager;

WatcherProcessManager* manager;


// In order to make callbacks on Watcher, we create a proxy
// WatcherProcess. The ZooKeeperImpl (defined below) dispatches
// "events" to the WatcherProcess which then invokes
// Watcher::process. The major benefit of this approach is that a
// WatcherProcess lifetime can precisely match the lifetime of a
// Watcher, so the ZooKeeperImpl won't end up calling into an object
// that has been deleted. In the worst case, the ZooKeeperImpl will
// dispatch to a dead WatcherProcess, which will just get dropped on
// the floor. In addition, the callbacks in the Watcher can manipulate
// the ZooKeeper object freely, calling delete on it if necessary
// (e.g., after a session expiration). We wanted to keep the Watcher
// interface clean and simple, so rather than add a member in Watcher
// that points to a WatcherProcess instance (or points to a
// WatcherImpl), we choose to create a WatcherProcessManager that
// stores the Watcher and WatcherProcess associations. The
// WatcherProcessManager is akin to having a shared dictionary or
// hashtable and using locks to access it rather then sending and
// receiving messages. Their is probably a performance hit here, but
// it would be interesting to see how bad the perforamnce is across a
// range of low and high-contention states.
class WatcherProcess : public Process<WatcherProcess>
{
public:
  WatcherProcess(Watcher* watcher) : watcher(watcher) {}

  void event(ZooKeeper* zk, int type, int state, const string& path)
  {
    watcher->process(zk, type, state, path);
  }

private:
  Watcher* watcher;
};


class WatcherProcessManager : public Process<WatcherProcessManager>
{
public:
  WatcherProcess* create(Watcher* watcher)
  {
    WatcherProcess* process = new WatcherProcess(watcher);
    spawn(process);
    processes[watcher] = process;
    return process;
  }

  bool destroy(Watcher* watcher)
  {
   if (processes.count(watcher) > 0) {
      WatcherProcess* process = processes[watcher];
      processes.erase(watcher);
      process::post(process->self(), process::TERMINATE);
      process::wait(process->self());
      delete process;
      return true;
    }

    return false;
  }

  PID<WatcherProcess> lookup(Watcher* watcher)
  {
    if (processes.count(watcher) > 0) {
      return processes[watcher]->self();
    }

    return PID<WatcherProcess>();
  }

private:
  map<Watcher*, WatcherProcess*> processes;
};


Watcher::Watcher()
{
  // Confirm we have created the WatcherProcessManager.
  static volatile bool initialized = false;
  static volatile bool initializing = true;

  // Confirm everything is initialized.
  if (!initialized) {
    if (__sync_bool_compare_and_swap(&initialized, false, true)) {
      manager = new WatcherProcessManager();
      process::spawn(manager);
      initializing = false;
    }
  }

  while (initializing);

  WatcherProcess* process =
    process::call(manager->self(), &WatcherProcessManager::create, this);

  if (process == NULL) {
    fatal("failed to initialize Watcher");
  }
}


Watcher::~Watcher()
{
  process::call(manager->self(), &WatcherProcessManager::destroy, this);
}


#ifndef USE_THREADED_ZOOKEEPER
class ZooKeeperImpl : public Process<ZooKeeperImpl>
#else
class ZooKeeperImpl
#endif // USE_THREADED_ZOOKEEPER
{
public:
  ZooKeeperImpl(ZooKeeper* zk,
                const string& servers,
                const milliseconds& timeout,
		Watcher* watcher)
    : zk(zk), servers(servers), timeout(timeout), watcher(watcher)
  {
    if (watcher == NULL) {
      fatalerror("cannot instantiate ZooKeeper with NULL watcher");
    }

    // Lookup PID of the WatcherProcess associated with the Watcher.
    pid = call(manager->self(), &WatcherProcessManager::lookup, watcher);

    // N.B. The Watcher and thus WatcherProcess may already be gone,
    // in which case, each dispatch to the WatcherProcess that we do
    // will just get dropped on the floor.

    // TODO(benh): Link with WatcherProcess PID?

    zh = zookeeper_init(servers.c_str(), event, timeout.value, NULL, this, 0);
    if (zh == NULL) {
      fatalerror("failed to create ZooKeeper (zookeeper_init)");
    }
  }

  ~ZooKeeperImpl()
  {
    int ret = zookeeper_close(zh);
    if (ret != ZOK) {
      fatal("failed to destroy ZooKeeper (zookeeper_close): %s", zerror(ret));
    }
  }

  Promise<int> authenticate(const string& scheme, const string& credentials)
  {
    Promise<int> promise;

    tuple<Promise<int> >* args = new tuple<Promise<int> >(promise);

    int ret = zoo_add_auth(zh, scheme.c_str(), credentials.data(),
                           credentials.size(), voidCompletion, args);

    if (ret != ZOK) {
      promise.set(ret);
      delete args;
    }

    return promise;
  }

  Promise<int> create(const string& path, const string& data,
                      const ACL_vector& acl, int flags, string* result)
  {
    Promise<int> promise;

    tuple<Promise<int>, string*>* args =
      new tuple<Promise<int>, string*>(promise, result);

    int ret = zoo_acreate(zh, path.c_str(), data.data(), data.size(), &acl,
                          flags, stringCompletion, args);

    if (ret != ZOK) {
      promise.set(ret);
      delete args;
    }

    return promise;
  }

  Promise<int> remove(const string& path, int version)
  {
    Promise<int> promise;

    tuple<Promise<int> >* args = new tuple<Promise<int> >(promise);

    int ret = zoo_adelete(zh, path.c_str(), version, voidCompletion, args);

    if (ret != ZOK) {
      promise.set(ret);
      delete args;
    }

    return promise;
  }

  Promise<int> exists(const string& path, bool watch, Stat* stat)
  {
    Promise<int> promise;

    tuple<Promise<int>, Stat*>* args =
      new tuple<Promise<int>, Stat*>(promise, stat);

    int ret = zoo_aexists(zh, path.c_str(), watch, statCompletion, args);

    if (ret != ZOK) {
      promise.set(ret);
      delete args;
    }

    return promise;
  }

  Promise<int> get(const string& path, bool watch, string* result, Stat* stat)
  {
    Promise<int> promise;

    tuple<Promise<int>, string*, Stat*>* args =
      new tuple<Promise<int>, string*, Stat*>(promise, result, stat);

    int ret = zoo_aget(zh, path.c_str(), watch, dataCompletion, args);

    if (ret != ZOK) {
      promise.set(ret);
      delete args;
    }

    return promise;
  }

  Promise<int> getChildren(const string& path, bool watch,
                           vector<string>* results)
  {
    Promise<int> promise;

    tuple<Promise<int>, vector<string>*>* args =
      new tuple<Promise<int>, vector<string>*>(promise, results);

    int ret = zoo_aget_children(zh, path.c_str(), watch, stringsCompletion,
                                args);

    if (ret != ZOK) {
      promise.set(ret);
      delete args;
    }

    return promise;
  }

  Promise<int> set(const string& path, const string& data, int version)
  {
    Promise<int> promise;

    tuple<Promise<int>, Stat*>* args =
      new tuple<Promise<int>, Stat*>(promise, NULL);

    int ret = zoo_aset(zh, path.c_str(), data.data(), data.size(),
                       version, statCompletion, args);

    if (ret != ZOK) {
      promise.set(ret);
      delete args;
    }

    return promise;
  }

#ifndef USE_THREADED_ZOOKEEPER
protected:
  virtual void operator () ()
  {
    while (true) {
      int fd;
      int ops;
      timeval tv;

      prepare(&fd, &ops, &tv);

      double secs = tv.tv_sec + (tv.tv_usec * 1e-6);

      // Cause await to return immediately if the file descriptor is
      // not valid (for example because the connection timed out) and
      // secs is 0 because that will block indefinitely.
      if (fd == -1 && secs == 0) {
	secs = -1;
      }

      if (poll(fd, ops, secs, false)) {
	// Either timer expired (might be 0) or data became available on fd.
	process(fd, ops);
      } else {
        // Okay, a message must have been received. Handle only one
        // message at a time so as not to delay any necessary internal
        // processing.
        serve(0, true);
        if (name() == process::TERMINATE) {
          return;
        } else if (name() != process::NOTHING) {
          fatal("unexpected interruption of 'poll'");
        }
      }
    }
  }

  bool prepare(int* fd, int* ops, timeval* tv)
  {
    int interest = 0;

    int ret = zookeeper_interest(zh, fd, &interest, tv);

    // If in some disconnected state, try again later.
    if (ret == ZINVALIDSTATE ||
        ret == ZCONNECTIONLOSS ||
	ret == ZOPERATIONTIMEOUT) {
      return false;
    }

    if (ret != ZOK) {
      fatal("zookeeper_interest failed! (%s)", zerror(ret));
    }

    *ops = 0;

    if ((interest & ZOOKEEPER_READ) && (interest & ZOOKEEPER_WRITE)) {
      *ops |= RDWR;
    } else if (interest & ZOOKEEPER_READ) {
      *ops |= RDONLY;
    } else if (interest & ZOOKEEPER_WRITE) {
      *ops |= WRONLY;
    }

    return true;
  }

  void process(int fd, int ops)
  {
    int events = 0;

    if (ready(fd, RDONLY)) {
      events |= ZOOKEEPER_READ;
    } if (ready(fd, WRONLY)) {
      events |= ZOOKEEPER_WRITE;
    }

    int ret = zookeeper_process(zh, events);

    // If in some disconnected state, try again later.
    if (ret == ZINVALIDSTATE || ret == ZCONNECTIONLOSS) {
      return;
    }

    if (ret != ZOK && ret != ZNOTHING) {
      fatal("zookeeper_process failed! (%s)", zerror(ret));
    }
  }
#endif // USE_THREADED_ZOOKEEPER

private:
  static void event(zhandle_t* zh, int type, int state,
		    const char* path, void* ctx)
  {
    ZooKeeperImpl* impl = static_cast<ZooKeeperImpl*>(ctx);
    process::dispatch(impl->pid, &WatcherProcess::event,
		      impl->zk, type, state, string(path));
  }


  static void voidCompletion(int ret, const void *data)
  {
    const tuple<Promise<int> >* args =
      reinterpret_cast<const tuple<Promise<int> >*>(data);

    Promise<int> promise = (*args).get<0>();

    promise.set(ret);

    delete args;
  }


  static void stringCompletion(int ret, const char* value, const void* data)
  {
    const tuple<Promise<int>, string*> *args =
      reinterpret_cast<const tuple<Promise<int>, string*>*>(data);

    Promise<int> promise = (*args).get<0>();
    string* result = (*args).get<1>();

    if (ret == 0) {
      if (result != NULL) {
	result->assign(value);
      }
    }

    promise.set(ret);

    delete args;
  }


  static void statCompletion(int ret, const Stat* stat, const void* data)
  {
    const tuple<Promise<int>, Stat*>* args =
      reinterpret_cast<const tuple<Promise<int>, Stat*>*>(data);

    Promise<int> promise = (*args).get<0>();
    Stat *stat_result = (*args).get<1>();

    if (ret == 0) {
      if (stat_result != NULL) {
	*stat_result = *stat;
      }
    }

    promise.set(ret);

    delete args;
  }


  static void dataCompletion(int ret, const char* value, int value_len,
			     const Stat* stat, const void* data)
  {
    const tuple<Promise<int>, string*, Stat*>* args =
      reinterpret_cast<const tuple<Promise<int>, string*, Stat*>*>(data);

    Promise<int> promise = (*args).get<0>();
    string* result = (*args).get<1>();
    Stat* stat_result = (*args).get<2>();

    if (ret == 0) {
      if (result != NULL) {
	result->assign(value, value_len);
      }

      if (stat_result != NULL) {
	*stat_result = *stat;
      }
    }

    promise.set(ret);

    delete args;
  }


  static void stringsCompletion(int ret, const String_vector* values,
				const void* data)
  {
    const tuple<Promise<int>, vector<string>*>* args =
      reinterpret_cast<const tuple<Promise<int>, vector<string>*>*>(data);

    Promise<int> promise = (*args).get<0>();
    vector<string>* results = (*args).get<1>();

    if (ret == 0) {
      if (results != NULL) {
	for (int i = 0; i < values->count; i++) {
	  results->push_back(values->data[i]);
	}
      }
    }

    promise.set(ret);

    delete args;
  }

private:
  friend class ZooKeeper;

  const string servers; // ZooKeeper host:port pairs.
  const milliseconds timeout; // ZooKeeper session timeout.

  ZooKeeper* zk; // ZooKeeper instance.
  zhandle_t* zh; // ZooKeeper connection handle.

  Watcher* watcher; // Associated Watcher instance.
  PID<WatcherProcess> pid; // PID of WatcherProcess that invokes Watcher.
};


ZooKeeper::ZooKeeper(const string& servers,
                     const milliseconds& timeout,
                     Watcher* watcher)
{
  impl = new ZooKeeperImpl(this, servers, timeout, watcher);
#ifndef USE_THREADED_ZOOKEEPER
  process::spawn(impl);
#endif // USE_THREADED_ZOOKEEPER
}


ZooKeeper::~ZooKeeper()
{
#ifndef USE_THREADED_ZOOKEEPER
  process::post(impl->self(), process::TERMINATE);
  process::wait(impl->self());
#endif // USE_THREADED_ZOOKEEPER
  delete impl;
}


int ZooKeeper::getState()
{
  return zoo_state(impl->zh);
}


int64_t ZooKeeper::getSessionId()
{
  return zoo_client_id(impl->zh)->client_id;
}


int ZooKeeper::authenticate(const string& scheme, const string& credentials)
{
#ifndef USE_THREADED_ZOOKEEPER
  return process::call(impl->self(), &ZooKeeperImpl::authenticate,
                       cref(scheme), cref(credentials));
#else
  Promise<int> promise = impl->authenticate(scheme, credentials);
  return promise.future().get();
#endif // USE_THREADED_ZOOKEEPER
}


int ZooKeeper::create(const string& path, const string& data,
                      const ACL_vector& acl, int flags, string* result)
{
#ifndef USE_THREADED_ZOOKEEPER
  return process::call(impl->self(), &ZooKeeperImpl::create,
                       cref(path), cref(data), cref(acl), flags, result);
#else
  Promise<int> promise = impl->create(path, data, acl, flags, result);
  return promise.future().get();
#endif // USE_THREADED_ZOOKEEPER
}


int ZooKeeper::remove(const string& path, int version)
{
#ifndef USE_THREADED_ZOOKEEPER
  return process::call(impl->self(), &ZooKeeperImpl::remove,
                       cref(path), version);
#else
  Promise<int> promise = impl->remove(path, version);
  return promise.future().get();
#endif // USE_THREADED_ZOOKEEPER
}


int ZooKeeper::exists(const string& path, bool watch, Stat* stat)
{
#ifndef USE_THREADED_ZOOKEEPER
  return process::call(impl->self(), &ZooKeeperImpl::exists,
                       cref(path), watch, stat);
#else
  Promise<int> promise = impl->exists(path, watch, stat);
  return promise.future().get();
#endif // USE_THREADED_ZOOKEEPER
}


int ZooKeeper::get(const string& path, bool watch, string* result, Stat* stat)
{
#ifndef USE_THREADED_ZOOKEEPER
  return process::call(impl->self(), &ZooKeeperImpl::get,
                       cref(path), watch, result, stat);
#else
  Promise<int> promise = impl->get(path, watch, result, stat);
  return promise.future().get();
#endif // USE_THREADED_ZOOKEEPER
}


int ZooKeeper::getChildren(const string& path, bool watch,
                           vector<string>* results)
{
#ifndef USE_THREADED_ZOOKEEPER
  return process::call(impl->self(), &ZooKeeperImpl::getChildren,
                       cref(path), watch, results);
#else
  Promise<int> promise = impl->getChildren(path, watch, results);
  return promise.future().get();
#endif // USE_THREADED_ZOOKEEPER
}


int ZooKeeper::set(const string& path, const string& data, int version)
{
#ifndef USE_THREADED_ZOOKEEPER
  return process::call(impl->self(), &ZooKeeperImpl::set,
                       cref(path), cref(data), version);
#else
  Promise<int> promise = impl->set(path, data, version);
  return promise.future().get();
#endif // USE_THREADED_ZOOKEEPER
}


const char* ZooKeeper::message(int code) const
{
  return zerror(code);
}


bool ZooKeeper::retryable(int code)
{
  switch (code) {
    case ZCONNECTIONLOSS:
    case ZOPERATIONTIMEOUT:
    case ZSESSIONEXPIRED:
    case ZSESSIONMOVED:
      return true;

    case ZOK: // No need to retry!

    case ZSYSTEMERROR: // Should not be encountered, here for completeness.
    case ZRUNTIMEINCONSISTENCY:
    case ZDATAINCONSISTENCY:
    case ZMARSHALLINGERROR:
    case ZUNIMPLEMENTED:
    case ZBADARGUMENTS:
    case ZINVALIDSTATE:

    case ZAPIERROR: // Should not be encountered, here for completeness.
    case ZNONODE:
    case ZNOAUTH:
    case ZBADVERSION:
    case ZNOCHILDRENFOREPHEMERALS:
    case ZNODEEXISTS:
    case ZNOTEMPTY:
    case ZINVALIDCALLBACK:
    case ZINVALIDACL:
    case ZAUTHFAILED:
    case ZCLOSING:
    case ZNOTHING: // Is this used? It's not exposed in the Java API.
      return false;

    default:
      LOG(FATAL) << "Unknown ZooKeeper code: " << code;
  }
}
