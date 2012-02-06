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

#include <signal.h>

#include <glog/logging.h>

#include <iostream>
#include <string>
#include <sstream>

#include <mesos/executor.hpp>

#include <process/dispatch.hpp>
#include <process/process.hpp>
#include <process/protobuf.hpp>

#include "common/fatal.hpp"
#include "common/lock.hpp"
#include "common/logging.hpp"
#include "common/type_utils.hpp"
#include "common/utils.hpp"
#include "common/uuid.hpp"

#include "messages/messages.hpp"

using namespace mesos;
using namespace mesos::internal;

using namespace process;

using std::string;

using process::wait; // Necessary on some OS's to disambiguate.


namespace mesos {
namespace internal {

class ExecutorProcess : public ProtobufProcess<ExecutorProcess>
{
public:
  ExecutorProcess(const UPID& _slave,
                  MesosExecutorDriver* _driver,
                  Executor* _executor,
                  const FrameworkID& _frameworkId,
                  const ExecutorID& _executorId,
                  bool _local,
                  const std::string& _directory)
    : slave(_slave),
      driver(_driver),
      executor(_executor),
      frameworkId(_frameworkId),
      executorId(_executorId),
      local(_local),
      aborted(false),
      directory(_directory)
  {
    install<ExecutorRegisteredMessage>(
        &ExecutorProcess::registered,
        &ExecutorRegisteredMessage::args);

    install<RunTaskMessage>(
        &ExecutorProcess::runTask,
        &RunTaskMessage::task);

    install<KillTaskMessage>(
        &ExecutorProcess::killTask,
        &KillTaskMessage::task_id);

    install<FrameworkToExecutorMessage>(
        &ExecutorProcess::frameworkMessage,
        &FrameworkToExecutorMessage::slave_id,
        &FrameworkToExecutorMessage::framework_id,
        &FrameworkToExecutorMessage::executor_id,
        &FrameworkToExecutorMessage::data);

    install<ShutdownExecutorMessage>(
        &ExecutorProcess::shutdown);
  }

  virtual ~ExecutorProcess() {}

protected:
  virtual void initialize()
  {
    VLOG(1) << "Executor started at: " << self();

    link(slave);

    // Register with slave.
    RegisterExecutorMessage message;
    message.mutable_framework_id()->MergeFrom(frameworkId);
    message.mutable_executor_id()->MergeFrom(executorId);
    send(slave, message);
  }

  void registered(const ExecutorArgs& args)
  {
    if (aborted) {
      VLOG(1) << "Ignoring registered message because the driver is aborted!";
      return;
    }

    VLOG(1) << "Executor registered on slave " << args.slave_id();

    slaveId = args.slave_id();
    executor->init(driver, args);
  }

  void runTask(const TaskDescription& task)
  {
    if (aborted) {
      VLOG(1) << "Ignore run task message because the driver is aborted!";
      return;
    }

    VLOG(1) << "Executor asked to run task '" << task.task_id() << "'";

    executor->launchTask(driver, task);
  }

  void killTask(const TaskID& taskId)
  {
    if (aborted) {
      VLOG(1) << "Ignoring kill task message because the driver is aborted!";
      return;
    }

    VLOG(1) << "Executor asked to kill task '" << taskId << "'";

    executor->killTask(driver, taskId);
  }

  void frameworkMessage(const SlaveID& slaveId,
			const FrameworkID& frameworkId,
			const ExecutorID& executorId,
			const string& data)
  {
    if (aborted) {
      VLOG(1) << "Ignoring framework message because the driver is aborted!";
      return;
    }

    VLOG(1) << "Executor received framework message";

    executor->frameworkMessage(driver, data);
  }

  void shutdown()
  {
    if (aborted) {
      VLOG(1) << "Ignoring shutdown message because the driver is aborted!";
      return;
    }

    VLOG(1) << "Executor asked to shutdown";

    // TODO(benh): Any need to invoke driver.stop?
    executor->shutdown(driver);
    if (!local) {
      exit(0);
    } else {
      terminate(this);
    }
  }

  void abort()
  {
    VLOG(1) << "De-activating the executor libprocess";
    aborted = true;
  }

  virtual void exited(const UPID& pid)
  {
    if (aborted) {
      VLOG(1) << "Ignoring exited event because the driver is aborted!";
      return;
    }

    VLOG(1) << "Slave exited, trying to shutdown";

    // TODO: Pass an argument to shutdown to tell it this is abnormal?
    executor->shutdown(driver);

    // This is a pretty bad state ... no slave is left. Rather
    // than exit lets kill our process group (which includes
    // ourself) hoping to clean up any processes this executor
    // launched itself.
    // TODO(benh): Maybe do a SIGTERM and then later do a SIGKILL?
    if (!local) {
      killpg(0, SIGKILL);
    } else {
      terminate(this);
    }
  }

  void sendStatusUpdate(const TaskStatus& status)
  {
    StatusUpdateMessage message;
    StatusUpdate* update = message.mutable_update();
    update->mutable_framework_id()->MergeFrom(frameworkId);
    update->mutable_executor_id()->MergeFrom(executorId);
    update->mutable_slave_id()->MergeFrom(slaveId);
    update->mutable_status()->MergeFrom(status);
    update->set_timestamp(Clock::now());
    update->set_uuid(UUID::random().toBytes());
    send(slave, message);
  }

  void sendFrameworkMessage(const string& data)
  {
    ExecutorToFrameworkMessage message;
    message.mutable_slave_id()->MergeFrom(slaveId);
    message.mutable_framework_id()->MergeFrom(frameworkId);
    message.mutable_executor_id()->MergeFrom(executorId);
    message.set_data(data);
    send(slave, message);
  }

private:
  friend class mesos::MesosExecutorDriver;

  UPID slave;
  MesosExecutorDriver* driver;
  Executor* executor;
  FrameworkID frameworkId;
  ExecutorID executorId;
  SlaveID slaveId;
  bool local;
  bool aborted;
  const std::string directory;
};

} // namespace internal {
} // namespace mesos {


// Implementation of C++ API.


MesosExecutorDriver::MesosExecutorDriver(Executor* _executor)
  : executor(_executor), state(INITIALIZED), process(NULL)
{
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  // Create mutex and condition variable
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&mutex, &attr);
  pthread_mutexattr_destroy(&attr);
  pthread_cond_init(&cond, 0);

  // TODO(benh): Initialize glog.

  // Initialize libprocess library (but not glog, done above).
  process::initialize(false);
}


MesosExecutorDriver::~MesosExecutorDriver()
{
  // Just as in SchedulerProcess, we might wait here indefinitely if
  // MesosExecutorDriver::stop has not been invoked.
  wait(process);
  delete process;

  pthread_mutex_destroy(&mutex);
  pthread_cond_destroy(&cond);
}


Status MesosExecutorDriver::start()
{
  Lock lock(&mutex);

  if (state == RUNNING) {
    return DRIVER_ALREADY_RUNNING;
  } else if (state == STOPPED) {
    return DRIVER_STOPPED;
  } else if (state == ABORTED) {
    return DRIVER_ABORTED;
  }

  // Set stream buffering mode to flush on newlines so that we capture logs
  // from user processes even when output is redirected to a file.
  setvbuf(stdout, 0, _IOLBF, 0);
  setvbuf(stderr, 0, _IOLBF, 0);

  bool local;

  UPID slave;
  FrameworkID frameworkId;
  ExecutorID executorId;
  std::string workDirectory;

  char* value;
  std::istringstream iss;

  /* Check if this is local (for example, for testing). */
  value = getenv("MESOS_LOCAL");

  if (value != NULL) {
    local = true;
  } else {
    local = false;
  }

  /* Get slave PID from environment. */
  value = getenv("MESOS_SLAVE_PID");

  if (value == NULL) {
    fatal("expecting MESOS_SLAVE_PID in environment");
  }

  slave = UPID(value);

  if (!slave) {
    fatal("cannot parse MESOS_SLAVE_PID");
  }

  /* Get framework ID from environment. */
  value = getenv("MESOS_FRAMEWORK_ID");

  if (value == NULL) {
    fatal("expecting MESOS_FRAMEWORK_ID in environment");
  }

  frameworkId.set_value(value);

  /* Get executor ID from environment. */
  value = getenv("MESOS_EXECUTOR_ID");

  if (value == NULL) {
    fatal("expecting MESOS_EXECUTOR_ID in environment");
  }

  executorId.set_value(value);

  /* Get working directory from environment */
  value = getenv("MESOS_DIRECTORY");

  if (value == NULL) {
    fatal("expecting MESOS_DIRECTORY in environment");
  }

  workDirectory = value;

  CHECK(process == NULL);

  process =
    new ExecutorProcess(slave, this, executor, frameworkId,
                        executorId, local, workDirectory);

  spawn(process);

  state = RUNNING;

  return OK;
}


Status MesosExecutorDriver::stop()
{
  Lock lock(&mutex);

  if (state == STOPPED) {
    return DRIVER_STOPPED;
  } else if (state != RUNNING && state != ABORTED) {
    return DRIVER_NOT_RUNNING;
  }

  CHECK(process != NULL);

  terminate(process);

  state = STOPPED;

  // TODO(benh): Set the condition variable in ExecutorProcess just as
  // we do with the MesosSchedulerDriver and SchedulerProcess:
  // dispatch(process, &ExecutorProcess::stop);

  pthread_cond_signal(&cond);

  return OK;
}


Status MesosExecutorDriver::abort()
{
  Lock lock(&mutex);

  if (state == ABORTED) {
    return DRIVER_ABORTED;
  } else if (state == STOPPED) {
    return DRIVER_STOPPED;
  } else if (state != RUNNING) {
    return DRIVER_NOT_RUNNING;
  }

  state = ABORTED;

  CHECK(process != NULL);

  // TODO(benh): Set the condition variable in ExecutorProcess just as
  // we do with the MesosSchedulerDriver and SchedulerProcess.

  dispatch(process, &ExecutorProcess::abort);

  pthread_cond_signal(&cond);

  return OK;
}


Status MesosExecutorDriver::join()
{
  Lock lock(&mutex);

  if (state == ABORTED) {
    return DRIVER_ABORTED;
  } else if (state == STOPPED) {
    return DRIVER_STOPPED;
  } else if (state != RUNNING) {
    return DRIVER_NOT_RUNNING;
  }

  while (state == RUNNING) {
    pthread_cond_wait(&cond, &mutex);
  }

  if (state == ABORTED) {
    return DRIVER_ABORTED;
  }

  CHECK(state == STOPPED);

  return OK;
}


Status MesosExecutorDriver::run()
{
  Status status = start();
  return status != OK ? status : join();
}


Status MesosExecutorDriver::sendStatusUpdate(const TaskStatus& status)
{
  Lock lock(&mutex);

  if (state == ABORTED) {
    return DRIVER_ABORTED;
  } else if (state != RUNNING) {
    return DRIVER_NOT_RUNNING;
  }

  CHECK(process != NULL);

  dispatch(process, &ExecutorProcess::sendStatusUpdate, status);

  return OK;
}


Status MesosExecutorDriver::sendFrameworkMessage(const string& data)
{
  Lock lock(&mutex);

  if (state == ABORTED) {
    return DRIVER_ABORTED;
  } else if (state != RUNNING) {
    return DRIVER_NOT_RUNNING;
  }

  CHECK(process != NULL);

  dispatch(process, &ExecutorProcess::sendFrameworkMessage, data);

  return OK;
}
