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

#ifdef __APPLE__
// Since Python.h defines _XOPEN_SOURCE on Mac OS X, we undefine it
// here so that we don't get warning messages during the build.
#undef _XOPEN_SOURCE
#endif // __APPLE__
#include <Python.h>

#include "mesos_scheduler_driver_impl.hpp"
#include "module.hpp"
#include "proxy_scheduler.hpp"

using namespace mesos;
using namespace mesos::python;

using std::cerr;
using std::endl;
using std::string;
using std::vector;
using std::map;


namespace mesos { namespace python {

/**
 * Python type object for MesosSchedulerDriverImpl.
 */
PyTypeObject MesosSchedulerDriverImplType = {
  PyObject_HEAD_INIT(NULL)
  0,                                                /* ob_size */
  "_mesos.MesosSchedulerDriverImpl",                /* tp_name */
  sizeof(MesosSchedulerDriverImpl),                 /* tp_basicsize */
  0,                                                /* tp_itemsize */
  (destructor) MesosSchedulerDriverImpl_dealloc,    /* tp_dealloc */
  0,                                                /* tp_print */
  0,                                                /* tp_getattr */
  0,                                                /* tp_setattr */
  0,                                                /* tp_compare */
  0,                                                /* tp_repr */
  0,                                                /* tp_as_number */
  0,                                                /* tp_as_sequence */
  0,                                                /* tp_as_mapping */
  0,                                                /* tp_hash */
  0,                                                /* tp_call */
  0,                                                /* tp_str */
  0,                                                /* tp_getattro */
  0,                                                /* tp_setattro */
  0,                                                /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,          /* tp_flags */
  "Private MesosSchedulerDriver implementation",    /* tp_doc */
  (traverseproc) MesosSchedulerDriverImpl_traverse, /* tp_traverse */
  (inquiry) MesosSchedulerDriverImpl_clear,         /* tp_clear */
  0,                                                /* tp_richcompare */
  0,                                                /* tp_weaklistoffset */
  0,                                                /* tp_iter */
  0,                                                /* tp_iternext */
  MesosSchedulerDriverImpl_methods,                 /* tp_methods */
  0,                                                /* tp_members */
  0,                                                /* tp_getset */
  0,                                                /* tp_base */
  0,                                                /* tp_dict */
  0,                                                /* tp_descr_get */
  0,                                                /* tp_descr_set */
  0,                                                /* tp_dictoffset */
  (initproc) MesosSchedulerDriverImpl_init,         /* tp_init */
  0,                                                /* tp_alloc */
  MesosSchedulerDriverImpl_new,                     /* tp_new */
};


/**
 * List of Python methods in MesosSchedulerDriverImpl.
 */
PyMethodDef MesosSchedulerDriverImpl_methods[] = {
  {"start", (PyCFunction) MesosSchedulerDriverImpl_start, METH_NOARGS,
   "Start the driver to connect to Mesos"},
  {"stop", (PyCFunction) MesosSchedulerDriverImpl_stop, METH_VARARGS,
   "Stop the driver, disconnecting from Mesos"},
  {"abort", (PyCFunction) MesosSchedulerDriverImpl_abort, METH_NOARGS,
    "Abort the driver, disabling calls from and to the driver"},
  {"join", (PyCFunction) MesosSchedulerDriverImpl_join, METH_NOARGS,
   "Wait for a running driver to disconnect from Mesos"},
  {"run", (PyCFunction) MesosSchedulerDriverImpl_run, METH_NOARGS,
   "Start a driver and run it, returning when it disconnects from Mesos"},
  {"requestResources",
   (PyCFunction) MesosSchedulerDriverImpl_requestResources,
   METH_VARARGS,
   "Request resources from the Mesos allocator"},
  {"launchTasks",
   (PyCFunction) MesosSchedulerDriverImpl_launchTasks,
   METH_VARARGS,
   "Reply to a Mesos offer with a list of tasks"},
  {"killTask",
   (PyCFunction) MesosSchedulerDriverImpl_killTask,
   METH_VARARGS,
   "Kill the task with the given ID"},
  {"reviveOffers",
   (PyCFunction) MesosSchedulerDriverImpl_reviveOffers,
   METH_NOARGS,
   "Remove all filters and ask Mesos for new offers"},
  {"sendFrameworkMessage",
   (PyCFunction) MesosSchedulerDriverImpl_sendFrameworkMessage,
   METH_VARARGS,
   "Send a FrameworkMessage to a slave"},
  {NULL}  /* Sentinel */
};


/**
 * Create, but don't initialize, a new MesosSchedulerDriverImpl
 * (called by Python before init method).
 */
PyObject* MesosSchedulerDriverImpl_new(PyTypeObject* type,
                                       PyObject* args,
                                       PyObject* kwds)
{
  MesosSchedulerDriverImpl* self;
  self = (MesosSchedulerDriverImpl*) type->tp_alloc(type, 0);
  if (self != NULL) {
    self->driver = NULL;
    self->proxyScheduler = NULL;
    self->pythonScheduler = NULL;
  }
  return (PyObject*) self;
}


/**
 * Initialize a MesosSchedulerDriverImpl with constructor arguments.
 */
int MesosSchedulerDriverImpl_init(MesosSchedulerDriverImpl* self,
                                  PyObject* args,
                                  PyObject* kwds)
{
  PyObject *pythonSchedulerObj = NULL;
  const char* url;
  PyObject *frameworkIdObj = NULL;
  const char* frameworkName;
  PyObject *executorInfoObj = NULL;

  if (!PyArg_ParseTuple(args, "OsOs|O", &pythonSchedulerObj, &frameworkName,
                        &executorInfoObj, &url, &frameworkIdObj)) {
    return -1;
  }

  if (pythonSchedulerObj != NULL) {
    PyObject* tmp = self->pythonScheduler;
    Py_INCREF(pythonSchedulerObj);
    self->pythonScheduler = pythonSchedulerObj;
    Py_XDECREF(tmp);
  }

  if (self->driver != NULL) {
    self->driver->stop();
    delete self->driver;
    self->driver = NULL;
  }

  if (self->proxyScheduler != NULL) {
    delete self->proxyScheduler;
    self->proxyScheduler = NULL;
  }

  FrameworkID frameworkId;
  if (frameworkIdObj != NULL) {
    if (!readPythonProtobuf(frameworkIdObj, &frameworkId)) {
      PyErr_Format(PyExc_Exception, "Could not deserialize Python FrameworkId");
      return -1;
    }
  }

  ExecutorInfo executorInfo;
  if (executorInfoObj != NULL) {
    if (!readPythonProtobuf(executorInfoObj, &executorInfo)) {
      PyErr_Format(PyExc_Exception, "Could not deserialize Python ExecutorInfo");
      return -1;
    }
  }

  self->proxyScheduler = new ProxyScheduler(self);

  if (frameworkIdObj != NULL) {
    self->driver = new MesosSchedulerDriver(self->proxyScheduler, frameworkName,
                                            executorInfo, url, frameworkId);
  } else {
    self->driver = new MesosSchedulerDriver(self->proxyScheduler, frameworkName,
                                            executorInfo, url);
  }

  return 0;
}


/**
 * Free a MesosSchedulerDriverImpl.
 */
void MesosSchedulerDriverImpl_dealloc(MesosSchedulerDriverImpl* self)
{
  if (self->driver != NULL) {
    self->driver->stop();
    // We need to wrap the driver destructor in an "allow threads"
    // macro since the MesosSchedulerDriver destructor waits for the
    // SchedulerProcess to terminate and there might be a thread that
    // is trying to acquire the GIL to call through the
    // ProxyScheduler. It will only be after this thread executes that
    // the SchedulerProcess might actually get a terminate.
    Py_BEGIN_ALLOW_THREADS
    delete self->driver;
    Py_END_ALLOW_THREADS
    self->driver = NULL;
  }

  if (self->proxyScheduler != NULL) {
    delete self->proxyScheduler;
    self->proxyScheduler = NULL;
  }

  MesosSchedulerDriverImpl_clear(self);
  self->ob_type->tp_free((PyObject*) self);
}


/**
 * Traverse fields of a MesosSchedulerDriverImpl on a cyclic GC search.
 * See http://docs.python.org/extending/newtypes.html.
 */
int MesosSchedulerDriverImpl_traverse(MesosSchedulerDriverImpl* self,
                                      visitproc visit,
                                      void* arg)
{
  Py_VISIT(self->pythonScheduler);
  return 0;
}


/**
 * Clear fields of a MesosSchedulerDriverImpl that can participate in
 * GC cycles. See http://docs.python.org/extending/newtypes.html.
 */
int MesosSchedulerDriverImpl_clear(MesosSchedulerDriverImpl* self)
{
  Py_CLEAR(self->pythonScheduler);
  return 0;
}


PyObject* MesosSchedulerDriverImpl_start(MesosSchedulerDriverImpl* self)
{
  if (self->driver == NULL) {
    PyErr_Format(PyExc_Exception, "MesosSchedulerDriverImpl.driver is NULL");
    return NULL;
  }

  Status status = self->driver->start();
  return PyInt_FromLong(status); // Sets an exception if creating the int fails
}


PyObject* MesosSchedulerDriverImpl_stop(MesosSchedulerDriverImpl* self,
                                        PyObject* args)
{
  if (self->driver == NULL) {
    PyErr_Format(PyExc_Exception, "MesosSchedulerDriverImpl.driver is NULL");
    return NULL;
  }

  bool failover = false; // Should match default in mesos.py.

  if (!PyArg_ParseTuple(args, "|b", &failover)) {
    return NULL;
  }

  Status status = self->driver->stop(failover);
  return PyInt_FromLong(status); // Sets an exception if creating the int fails
}


PyObject* MesosSchedulerDriverImpl_abort(MesosSchedulerDriverImpl* self)
{
  if (self->driver == NULL) {
    PyErr_Format(PyExc_Exception, "MesosSchedulerDriverImpl.driver is NULL");
    return NULL;
  }

  Status status = self->driver->abort();
  return PyInt_FromLong(status); // Sets an exception if creating the int fails
}


PyObject* MesosSchedulerDriverImpl_join(MesosSchedulerDriverImpl* self)
{
  if (self->driver == NULL) {
    PyErr_Format(PyExc_Exception, "MesosSchedulerDriverImpl.driver is NULL");
    return NULL;
  }

  Status status;
  Py_BEGIN_ALLOW_THREADS
  status = self->driver->join();
  Py_END_ALLOW_THREADS
  return PyInt_FromLong(status); // Sets an exception if creating the int fails
}


PyObject* MesosSchedulerDriverImpl_run(MesosSchedulerDriverImpl* self)
{
  if (self->driver == NULL) {
    PyErr_Format(PyExc_Exception, "MesosSchedulerDriverImpl.driver is NULL");
    return NULL;
  }

  Status status;
  Py_BEGIN_ALLOW_THREADS
  status = self->driver->run();
  Py_END_ALLOW_THREADS
  return PyInt_FromLong(status); // Sets an exception if creating the int fails
}


PyObject* MesosSchedulerDriverImpl_requestResources(MesosSchedulerDriverImpl* self,
                                                    PyObject* args)
{
  if (self->driver == NULL) {
    PyErr_Format(PyExc_Exception, "MesosSchedulerDriverImpl.driver is NULL");
    return NULL;
  }

  PyObject* requestsObj = NULL;
  vector<ResourceRequest> requests;

  if (!PyArg_ParseTuple(args, "O", &requestsObj)) {
    return NULL;
  }

  if (!PyList_Check(requestsObj)) {
    PyErr_Format(PyExc_Exception, "Parameter 2 to requestsResources is not a list");
    return NULL;
  }
  Py_ssize_t len = PyList_Size(requestsObj);
  for (int i = 0; i < len; i++) {
    PyObject* requestObj = PyList_GetItem(requestsObj, i);
    if (requestObj == NULL) {
      return NULL; // Exception will have been set by PyList_GetItem
    }
    ResourceRequest request;
    if (!readPythonProtobuf(requestObj, &request)) {
      PyErr_Format(PyExc_Exception,
                   "Could not deserialize Python ResourceRequest");
      return NULL;
    }
    requests.push_back(request);
  }

  Status status = self->driver->requestResources(requests);
  return PyInt_FromLong(status); // Sets an exception if creating the int fails
}


PyObject* MesosSchedulerDriverImpl_launchTasks(MesosSchedulerDriverImpl* self,
                                                PyObject* args)
{
  if (self->driver == NULL) {
    PyErr_Format(PyExc_Exception, "MesosSchedulerDriverImpl.driver is NULL");
    return NULL;
  }

  PyObject* offerIdObj = NULL;
  PyObject* tasksObj = NULL;
  PyObject* filtersObj = NULL;
  OfferID offerId;
  vector<TaskDescription> tasks;
  Filters filters;

  if (!PyArg_ParseTuple(args, "OO|O", &offerIdObj, &tasksObj, &filtersObj)) {
    return NULL;
  }

  if (!readPythonProtobuf(offerIdObj, &offerId)) {
    PyErr_Format(PyExc_Exception, "Could not deserialize Python OfferID");
    return NULL;
  }

  if (!PyList_Check(tasksObj)) {
    PyErr_Format(PyExc_Exception, "Parameter 2 to launchTasks is not a list");
    return NULL;
  }
  Py_ssize_t len = PyList_Size(tasksObj);
  for (int i = 0; i < len; i++) {
    PyObject* taskObj = PyList_GetItem(tasksObj, i);
    if (taskObj == NULL) {
      return NULL; // Exception will have been set by PyList_GetItem
    }
    TaskDescription task;
    if (!readPythonProtobuf(taskObj, &task)) {
      PyErr_Format(PyExc_Exception,
                   "Could not deserialize Python TaskDescription");
      return NULL;
    }
    tasks.push_back(task);
  }

  if (filtersObj != NULL) {
    if (!readPythonProtobuf(filtersObj, &filters)) {
      PyErr_Format(PyExc_Exception,
                   "Could not deserialize Python Filters");
      return NULL;
    }
  }

  Status status = self->driver->launchTasks(offerId, tasks, filters);
  return PyInt_FromLong(status); // Sets an exception if creating the int fails
}


PyObject* MesosSchedulerDriverImpl_killTask(MesosSchedulerDriverImpl* self,
                                            PyObject* args)
{
  if (self->driver == NULL) {
    PyErr_Format(PyExc_Exception, "MesosSchedulerDriverImpl.driver is NULL");
    return NULL;
  }

  PyObject* tidObj = NULL;
  TaskID tid;
  if (!PyArg_ParseTuple(args, "O", &tidObj)) {
    return NULL;
  }
  if (!readPythonProtobuf(tidObj, &tid)) {
    PyErr_Format(PyExc_Exception, "Could not deserialize Python TaskID");
    return NULL;
  }

  Status status = self->driver->killTask(tid);
  return PyInt_FromLong(status); // Sets an exception if creating the int fails
}


PyObject* MesosSchedulerDriverImpl_reviveOffers(MesosSchedulerDriverImpl* self)
{
  if (self->driver == NULL) {
    PyErr_Format(PyExc_Exception, "MesosSchedulerDriverImpl.driver is NULL");
    return NULL;
  }

  Status status = self->driver->reviveOffers();
  return PyInt_FromLong(status); // Sets an exception if creating the int fails
}


PyObject* MesosSchedulerDriverImpl_sendFrameworkMessage(
    MesosSchedulerDriverImpl* self,
    PyObject* args)
{
  if (self->driver == NULL) {
    PyErr_Format(PyExc_Exception, "MesosSchedulerDriverImpl.driver is NULL");
    return NULL;
  }


  PyObject* sidObj = NULL;
  PyObject* eidObj = NULL;
  SlaveID sid;
  ExecutorID eid;
  const char* data;
  if (!PyArg_ParseTuple(args, "OOs", &sidObj, &eidObj, &data)) {
    return NULL;
  }
  if (!readPythonProtobuf(sidObj, &sid)) {
    PyErr_Format(PyExc_Exception, "Could not deserialize Python SlaveID");
    return NULL;
  }
  if (!readPythonProtobuf(eidObj, &eid)) {
    PyErr_Format(PyExc_Exception, "Could not deserialize Python ExecutorID");
    return NULL;
  }

  Status status = self->driver->sendFrameworkMessage(sid, eid, data);
  return PyInt_FromLong(status); // Sets an exception if creating the int fails
}

}} /* namespace mesos { namespace python { */
