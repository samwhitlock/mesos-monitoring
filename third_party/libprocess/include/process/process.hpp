#ifndef __PROCESS_PROCESS_HPP__
#define __PROCESS_PROCESS_HPP__

#include <stdint.h>
#include <pthread.h>

#include <map>
#include <queue>

#include <tr1/functional>

#include <process/clock.hpp>
#include <process/event.hpp>
#include <process/filter.hpp>
#include <process/http.hpp>
#include <process/message.hpp>
#include <process/pid.hpp>

namespace process {

class ProcessBase : public EventVisitor
{
public:
  ProcessBase(const std::string& id = "");

  virtual ~ProcessBase();

  UPID self() const { return pid; }

protected:
  // Invoked when an event is serviced.
  virtual void serve(const Event& event)
  {
    event.visit(this);
  }

  // Callbacks used to visit (i.e., handle) a specific event.
  virtual void visit(const MessageEvent& event);
  virtual void visit(const DispatchEvent& event);
  virtual void visit(const HttpEvent& event);
  virtual void visit(const ExitedEvent& event);
  virtual void visit(const TerminateEvent& event);

  // Invoked when a process gets spawned.
  virtual void initialize() {}

  // Invoked when a process is terminated (unless visit is overriden).
  virtual void finalize() {}

  // Invoked when a linked process has exited (see link).
  virtual void exited(const UPID& pid) {}

  // Invoked when a linked process can no longer be monitored (see link).
  virtual void lost(const UPID& pid) {}

  // Puts a message at front of queue.
  void inject(
      const UPID& from,
      const std::string& name,
      const char* data = NULL,
      size_t length = 0);

  // Sends a message with data to PID.
  void send(
      const UPID& to,
      const std::string& name,
      const char* data = NULL,
      size_t length = 0);

  // Links with the specified PID. Linking with a process from within
  // the same "operating system process" is gauranteed to give you
  // perfect monitoring of that process. However, linking with a
  // process on another machine might result in receiving lost
  // callbacks due to the nature of a distributed environment.
  UPID link(const UPID& pid);

  // The default visit implementation for message events invokes
  // installed message handlers, or delegates the message to another
  // process (a delegate can be installed below but a message handler
  // always takes precedence over delegating). A message handler is
  // any function which takes two arguments, the "from" pid and the
  // message body.
  typedef std::tr1::function<void(const UPID&, const std::string&)>
  MessageHandler;

  // Setup a handler for a message.
  void install(
      const std::string& name,
      const MessageHandler& handler)
  {
    handlers.message[name] = handler;
  }

  template <typename T>
  void install(
      const std::string& name,
      void (T::*method)(const UPID&, const std::string&))
  {
    // Note that we use dynamic_cast here so a process can use
    // multiple inheritance if it sees so fit (e.g., to implement
    // multiple callback interfaces).
    MessageHandler handler =
      std::tr1::bind(method,
                     dynamic_cast<T*>(this),
                     std::tr1::placeholders::_1,
                     std::tr1::placeholders::_2);
    install(name, handler);
  }

  // Delegate incoming message's with the specified name to pid.
  void delegate(const std::string& name, const UPID& pid)
  {
    delegates[name] = pid;
  }

  // The default visit implementation for HTTP events invokes
  // installed HTTP handlers. A HTTP handler is any function which
  // takes an HttpRequest object and returns and HttpResponse.
  typedef std::tr1::function<Future<HttpResponse>(const HttpRequest&)>
  HttpRequestHandler;

  // Setup a handler for an HTTP request.
  void route(
      const std::string& name,
      const HttpRequestHandler& handler)
  {
    handlers.http[name] = handler;
  }

  template <typename T>
  void route(
      const std::string& name,
      Future<HttpResponse> (T::*method)(const HttpRequest&))
  {
    // Note that we use dynamic_cast here so a process can use
    // multiple inheritance if it sees so fit (e.g., to implement
    // multiple callback interfaces).
    HttpRequestHandler handler =
      std::tr1::bind(method, dynamic_cast<T*>(this),
                     std::tr1::placeholders::_1);
    route(name, handler);
  }

private:
  friend class SocketManager;
  friend class ProcessManager;
  friend class ProcessReference;
  friend void* schedule(void*);

  // Process states.
  enum { BOTTOM,
         READY,
	 RUNNING,
         BLOCKED,
	 FINISHED } state;

  // Mutex protecting internals. TODO(benh): Replace with a spinlock.
  pthread_mutex_t m;
  void lock() { pthread_mutex_lock(&m); }
  void unlock() { pthread_mutex_unlock(&m); }

  // Enqueue the specified message, request, or function call.
  void enqueue(Event* event, bool inject = false);

  // Queue of received events.
  std::deque<Event*> events;

  // Delegates for messages.
  std::map<std::string, UPID> delegates;

  // Handlers for messages and HTTP requests.
  struct {
    std::map<std::string, MessageHandler> message;
    std::map<std::string, HttpRequestHandler> http;
  } handlers;

  // Active references.
  int refs;

  // Process PID.
  UPID pid;
};


template <typename T>
class Process : public virtual ProcessBase {
public:
  Process(const std::string& id = "") : ProcessBase(id) {}

  // Returns pid of process; valid even before calling spawn.
  PID<T> self() const { return PID<T>(dynamic_cast<const T*>(this)); }
};


/**
 * Initialize the library.
 *
 * @param initialize_google_logging whether or not to initialize the
 *        Google Logging library (glog). If the application is also
 *        using glog, this should be set to false.
 */
void initialize(bool initialize_google_logging = true);


/**
 * Spawn a new process.
 *
 * @param process process to be spawned
 * @param manage boolean whether process should get garbage collected
 */
UPID spawn(ProcessBase* process, bool manage = false);

template <typename T>
PID<T> spawn(T* t, bool manage = false)
{
  if (!spawn(static_cast<ProcessBase*>(t), manage)) {
    return PID<T>();
  }

  return PID<T>(t);
}

template <typename T>
PID<T> spawn(T& t, bool manage = false)
{
  return spawn(&t, manage);
}


/**
 * Send a TERMINATE message to a process, injecting the message ahead
 * of all other messages queued up for that process if requested. Note
 * that currently terminate only works for local processes (in the
 * future we plan to make this more explicit via the use of a PID
 * instead of a UPID).
 *
 * @param inject if true message will be put on front of messae queue
 */
void terminate(const UPID& pid, bool inject = true);
void terminate(const ProcessBase& process, bool inject = true);
void terminate(const ProcessBase* process, bool inject = true);


/**
 * Wait for process to exit no more than specified seconds (returns
 * true if actually waited on a process).
 *
 * @param PID id of the process
 * @param secs max time to wait, 0 implies wait for ever
 */
bool wait(const UPID& pid, double secs = 0);
bool wait(const ProcessBase& process, double secs = 0);
bool wait(const ProcessBase* process, double secs = 0);


/**
 * Sends a message with data without a return address.
 *
 * @param to receiver
 * @param name message name
 * @param data data to send (gets copied)
 * @param length length of data
 */
void post(const UPID& to,
          const std::string& name,
          const char* data = NULL,
          size_t length = 0);


// Inline implementations of above.
inline void terminate(const ProcessBase& process, bool inject)
{
  terminate(process.self(), inject);
}


inline void terminate(const ProcessBase* process, bool inject)
{
  terminate(process->self(), inject);
}


inline bool wait(const ProcessBase& process, double secs)
{
  return wait(process.self(), secs);
}


inline bool wait(const ProcessBase* process, double secs)
{
  return wait(process->self(), secs);
}

} // namespace process {

#endif // __PROCESS_PROCESS_HPP__
