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

#include <libgen.h>

#include <cstdlib>
#include <iostream>
#include <sstream>

#include <boost/lexical_cast.hpp>

#include <mesos/scheduler.hpp>

using namespace mesos;
using namespace std;

using boost::lexical_cast;


class MyScheduler : public Scheduler
{
public:
  MyScheduler(int totalTasks_, double taskLen_,
      int threadsPerTask_, int64_t memToRequest_, int64_t memToHog_)
    : totalTasks(totalTasks_), taskLen(taskLen_),
      threadsPerTask(threadsPerTask_),
      memToRequest(memToRequest_), memToHog(memToHog_),
      tasksLaunched(0), tasksFinished(0) {}

  virtual ~MyScheduler() {}

  virtual void registered(SchedulerDriver*, const FrameworkID&)
  {
    cout << "Registered!" << endl;
  }

  virtual void resourceOffers(SchedulerDriver* driver,
                              const vector<Offer>& offers)
  {
    vector<Offer>::const_iterator iterator = offers.begin();
    for (; iterator != offers.end(); ++iterator) {
      const Offer& offer = *iterator;

      // Lookup resources we care about.
      // TODO(benh): It would be nice to ultimately have some helper
      // functions for looking up resources.
      double cpus = 0;
      double mem = 0;

      for (int i = 0; i < offer.resources_size(); i++) {
        const Resource& resource = offer.resources(i);
        if (resource.name() == "cpus" &&
            resource.type() == Value::SCALAR) {
          cpus = resource.scalar().value();
        } else if (resource.name() == "mem" &&
                   resource.type() == Value::SCALAR) {
          mem = resource.scalar().value();
        }
      }

      // Launch task (only one per offer).
      vector<TaskDescription> tasks;
      if ((tasksLaunched < totalTasks) && (cpus >= 1 && mem >= memToRequest)) {
        int taskId = tasksLaunched++;

        cout << "Starting task " << taskId << " on "
             << offer.hostname() << endl;

        TaskDescription task;
        task.set_name("Task " + lexical_cast<string>(taskId));
        task.mutable_task_id()->set_value(lexical_cast<string>(taskId));
        task.mutable_slave_id()->MergeFrom(offer.slave_id());

        Resource* resource;

        resource = task.add_resources();
        resource->set_name("cpus");
        resource->set_type(Value::SCALAR);
        resource->mutable_scalar()->set_value(1);

        resource = task.add_resources();
        resource->set_name("mem");
        resource->set_type(Value::SCALAR);
        resource->mutable_scalar()->set_value(memToRequest);

        ostringstream data;
        data << memToHog << " " << taskLen << " " << threadsPerTask;
        task.set_data(data.str());

        tasks.push_back(task);
      }

      driver->launchTasks(offer.id(), tasks);
    }
  }

  virtual void offerRescinded(SchedulerDriver* driver,
                              const OfferID& offerId) {}

  virtual void statusUpdate(SchedulerDriver* driver, const TaskStatus& status)
  {
    int taskId = lexical_cast<int>(status.task_id().value());

    cout << "Task " << taskId << " is in state " << status.state() << endl;

    if (status.state() == TASK_LOST)
      cout << "Task " << taskId
           << " lost. Not doing anything about it." << endl;

    if (status.state() == TASK_FINISHED)
      tasksFinished++;

    if (tasksFinished == totalTasks)
      driver->stop();
  }

  virtual void frameworkMessage(SchedulerDriver* driver,
				const SlaveID& slaveId,
				const ExecutorID& executorId,
                                const string& data) {}

  virtual void slaveLost(SchedulerDriver* driver, const SlaveID& sid) {}

  virtual void error(SchedulerDriver* driver, int code,
                     const std::string& message) {}

private:
  double taskLen;
  int threadsPerTask;
  int memToRequest;
  int memToHog;
  int tasksLaunched;
  int tasksFinished;
  int totalTasks;
};


int main(int argc, char** argv)
{
  if (argc != 7) {
    cerr << "Usage: " << argv[0]
         << " <master> <tasks> <task_len> <threads_per_task>"
         << " <MB_to_request> <MB_per_task>" << endl;
    return -1;
  }
  // Find this executable's directory to locate executor
  char buf[4096];
  realpath(dirname(argv[0]), buf);
  string uri = string(buf) + "/memhog-executor";
  MyScheduler sched(lexical_cast<int>(argv[2]),
                    lexical_cast<double>(argv[3]),
                    lexical_cast<int>(argv[4]),
                    lexical_cast<int64_t>(argv[5]),
                    lexical_cast<int64_t>(argv[6]));
  ExecutorInfo executor;
  executor.mutable_executor_id()->set_value("default");
  executor.set_uri(uri);
  MesosSchedulerDriver driver(&sched, "Memory hog", executor, argv[1]);
  driver.run();
  return 0;
}
