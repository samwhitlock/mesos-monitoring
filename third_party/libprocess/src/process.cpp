#include <errno.h>
#include <ev.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#include <arpa/inet.h>

#include <glog/logging.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#include <algorithm>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <vector>

#include <process/clock.hpp>
#include <process/deferred.hpp>
#include <process/dispatch.hpp>
#include <process/executor.hpp>
#include <process/filter.hpp>
#include <process/future.hpp>
#include <process/gc.hpp>
#include <process/process.hpp>
#include <process/timer.hpp>

#include "config.hpp"
#include "decoder.hpp"
#include "encoder.hpp"
#include "foreach.hpp"
#include "gate.hpp"
#include "synchronized.hpp"
#include "thread.hpp"


using std::deque;
using std::find;
using std::list;
using std::map;
using std::max;
using std::ostream;
using std::pair;
using std::queue;
using std::set;
using std::stack;
using std::string;
using std::stringstream;
using std::vector;

namespace lambda {

using std::tr1::bind;
using std::tr1::function;
using namespace std::tr1::placeholders;

} // namespace lambda {


#define Byte (1)
#define Kilobyte (1024*Byte)
#define Megabyte (1024*Kilobyte)
#define Gigabyte (1024*Megabyte)


template <int i>
std::ostream& fixedprecision(std::ostream& os)
{
  return os << std::fixed << std::setprecision(i);
}


struct Node
{
  Node(uint32_t _ip = 0, uint16_t _port = 0)
    : ip(_ip), port(_port) {}

  uint32_t ip;
  uint16_t port;
};


bool operator < (const Node& left, const Node& right)
{
  if (left.ip == right.ip)
    return left.port < right.port;
  else
    return left.ip < right.ip;
}


ostream& operator << (ostream& stream, const Node& node)
{
  stream << node.ip << ":" << node.port;
  return stream;
}


namespace process {

class ProcessReference
{
public:
  explicit ProcessReference(ProcessBase* _process) : process(_process)
  {
    if (process != NULL) {
      __sync_fetch_and_add(&(process->refs), 1);
    }
  }

  ~ProcessReference()
  {
    if (process != NULL)
      __sync_fetch_and_sub(&(process->refs), 1);
  }

  ProcessReference(const ProcessReference& that)
  {
    process = that.process;

    if (process != NULL) {
      // There should be at least one reference to the process, so
      // we don't need to worry about checking if it's exiting or
      // not, since we know we can always create another reference.
      CHECK(process->refs > 0);
      __sync_fetch_and_add(&(process->refs), 1);
    }
  }

  ProcessBase* operator -> ()
  {
    return process;
  }

  operator ProcessBase* ()
  {
    return process;
  }

  operator bool () const
  {
    return process != NULL;
  }

private:
  ProcessReference& operator = (const ProcessReference& that);

  ProcessBase* process;
};


class HttpProxy;


class HttpResponseWaiter
{
public:
  HttpResponseWaiter(const PID<HttpProxy>& proxy,
                     Future<HttpResponse>* future,
                     bool persist);

  void waited(const Future<HttpResponse>&);
  void timeout();

private:
  const PID<HttpProxy> proxy;
  Future<HttpResponse>* future;
  bool persist;

  Executor executor;
};


class HttpProxy : public Process<HttpProxy>
{
public:
  HttpProxy(int _c);

  void handle(Future<HttpResponse>* future, bool persist);
  void ready(Future<HttpResponse>* future, bool persist);
  void unavailable(Future<HttpResponse>* future, bool persist);

private:
  int c;
  map<Future<HttpResponse>*, HttpResponseWaiter*> waiters;
};


class SocketManager
{
public:
  SocketManager();
  ~SocketManager();

  void link(ProcessBase* process, const UPID& to);

  PID<HttpProxy> proxy(int s);

  void send(DataEncoder* encoder, int s, bool persist);
  void send(Message* message);

  DataEncoder* next(int s);

  void closed(int s);

  void exited(const Node& node);
  void exited(ProcessBase* process);

private:
  // Map from UPID (local/remote) to process.
  map<UPID, set<ProcessBase*> > links;

  // Map from socket to node (ip, port).
  map<int, Node> sockets;

  // Maps from node (ip, port) to socket.
  map<Node, int> temps;
  map<Node, int> persists;

  // Set of sockets that should be closed.
  set<int> disposables;

  // Map from socket to outgoing queue.
  map<int, queue<DataEncoder*> > outgoing;

  // HTTP proxies.
  map<int, HttpProxy*> proxies;

  // Protects instance variables.
  synchronizable(this);
};


class ProcessManager
{
public:
  ProcessManager();
  ~ProcessManager();

  ProcessReference use(const UPID& pid);

  bool deliver(Message* message, ProcessBase* sender = NULL);

  bool deliver(int c, HttpRequest* request, ProcessBase* sender = NULL);

  bool deliver(const UPID& to,
               lambda::function<void(ProcessBase*)>* f,
               ProcessBase* sender = NULL);

  UPID spawn(ProcessBase* process, bool manage);
  void resume(ProcessBase* process);
  void cleanup(ProcessBase* process);
  void link(ProcessBase* process, const UPID& to);
  void terminate(const UPID& pid, bool inject, ProcessBase* sender = NULL);
  bool wait(const UPID& pid);

  void enqueue(ProcessBase* process);
  ProcessBase* dequeue();

  void settle();

private:
  // Map of all local spawned and running processes.
  map<string, ProcessBase*> processes;
  synchronizable(processes);

  // Gates for waiting threads (protected by synchronizable(processes)).
  map<ProcessBase*, Gate*> gates;

  // Queue of runnable processes (implemented using list).
  list<ProcessBase*> runq;
  synchronizable(runq);

  // Number of running processes, to support Clock::settle operation.
  int running;
};


// Unique id that can be assigned to each process.
static uint32_t id = 0;

// Local server socket.
static int s = -1;

// Local IP address.
static uint32_t ip = 0;

// Local port.
static uint16_t port = 0;

// Active SocketManager (eventually will probably be thread-local).
static SocketManager* socket_manager = NULL;

// Active ProcessManager (eventually will probably be thread-local).
static ProcessManager* process_manager = NULL;

// Event loop.
static struct ev_loop* loop = NULL;

// Asynchronous watcher for interrupting loop.
static ev_async async_watcher;

// Watcher for timeouts.
static ev_timer timeouts_watcher;

// Server watcher for accepting connections.
static ev_io server_watcher;

// Queue of I/O watchers.
static queue<ev_io*>* watchers = new queue<ev_io*>();
static synchronizable(watchers) = SYNCHRONIZED_INITIALIZER;

// We store the timers in a map of lists indexed by the timeout of the
// timer so that we can have two timers that have the same timeout. We
// exploit that the map is SORTED!
static map<double, list<timer> >* timeouts =
  new map<double, list<timer> >();
static synchronizable(timeouts) = SYNCHRONIZED_INITIALIZER_RECURSIVE;

// For supporting Clock::settle(), true if timers have been removed
// from 'timeouts' but may not have been executed yet. Protected by
// the timeouts lock. This is only used when the clock is paused.
static bool pending_timers = false;

// Flag to indicate whether or to update the timer on async interrupt.
static bool update_timer = false;

const int NUMBER_OF_PROCESSING_THREADS = 4; // TODO(benh): Do 2x cores.


// Thread local process pointer magic (constructed in
// 'initialize'). We need the extra level of indirection from
// _process_ to __process__ so that we can take advantage of the
// operators without needing the extra dereference.
static ThreadLocal<ProcessBase>* _process_ = NULL;

#define __process__ (*_process_)


// Scheduler gate.
static Gate* gate = new Gate();

// Filter. Synchronized support for using the filterer needs to be
// recursive incase a filterer wants to do anything fancy (which is
// possible and likely given that filters will get used for testing).
static Filter* filterer = NULL;
static synchronizable(filterer) = SYNCHRONIZED_INITIALIZER_RECURSIVE;

// Global garbage collector.
PID<GarbageCollector> gc;

// Thunks to be invoked via process::invoke.
static queue<lambda::function<void(void)>*>* thunks =
  new queue<lambda::function<void(void)>*>();
static synchronizable(thunks) = SYNCHRONIZED_INITIALIZER;

// Thread to invoke thunks (see above).
static Gate* invoke_gate = new Gate();
static pthread_t invoke_thread;


// We namespace the clock related variables to keep them well
// named. In the future we'll probably want to associate a clock with
// a specific ProcessManager/SocketManager instance pair, so this will
// likely change.
namespace clock {

map<ProcessBase*, double>* currents = new map<ProcessBase*, double>();

double initial = 0;
double current = 0;

bool paused = false;

} // namespace clock {


double Clock::now()
{
  return now(__process__);
}


double Clock::now(ProcessBase* process)
{
  synchronized (timeouts) {
    if (Clock::paused()) {
      if (process != NULL) {
        if (clock::currents->count(process) != 0) {
          return (*clock::currents)[process];
        } else {
          return (*clock::currents)[process] = clock::initial;

        }
      } else {
        return clock::current;
      }
    }
  }
    
  return ev_time(); // TODO(benh): Versus ev_now()?
}


void Clock::pause()
{
  process::initialize(); // For the libev watchers to be setup.

  synchronized (timeouts) {
    if (!clock::paused) {
      clock::initial = clock::current = now();
      clock::paused = true;
      VLOG(1) << "Clock paused at "
              << std::fixed << std::setprecision(9) << clock::initial;
    }
  }

  // Note that after pausing the clock an existing libev timer might
  // still fire (invoking handle_timeout), but since paused == true no
  // "time" will actually have passed, so no timer will actually fire.
}


bool Clock::paused()
{
  return clock::paused;
}


void Clock::resume()
{
  process::initialize(); // For the libev watchers to be setup.

  synchronized (timeouts) {
    if (clock::paused) {
      VLOG(1) << "Clock resumed at "
              << std::fixed << std::setprecision(9) << clock::current;
      clock::paused = false;
      clock::currents->clear();
      update_timer = true;
      ev_async_send(loop, &async_watcher);
    }
  }
}


void Clock::advance(double secs)
{
  synchronized (timeouts) {
    if (clock::paused) {
      clock::current += secs;
      VLOG(1) << "Clock advanced ("
              << std::fixed << std::setprecision(9) << secs
              << " seconds) to " << clock::current;
      if (!update_timer) {
        update_timer = true;
        ev_async_send(loop, &async_watcher);
      }
    }
  }
}


void Clock::update(double secs)
{
  VLOG(2) << "Attempting to update clock to "
          << std::fixed << std::setprecision(9) << secs;
  synchronized (timeouts) {
    if (clock::paused) {
      if (clock::current < secs) {
        clock::current = secs;
        VLOG(1) << "Clock updated to "
                << std::fixed << std::setprecision(9) << clock::current;
        if (!update_timer) {
          update_timer = true;
          ev_async_send(loop, &async_watcher);
        }
      }
    }
  }
}


void Clock::update(ProcessBase* process, double secs)
{
  synchronized (timeouts) {
    if (clock::paused) {
      double current = now(process);
      if (current < secs) {
        VLOG(2) << "Clock of " << process->self() << " updated to "
                << std::fixed << std::setprecision(9) << secs;
        (*clock::currents)[process] = secs;
      }
    }
  }
}


void Clock::order(ProcessBase* from, ProcessBase* to)
{
  update(to, now(from));
}

void Clock::settle()
{
  CHECK(clock::paused); // TODO(benh): Consider returning a bool instead.
  process_manager->settle();
}


int set_nbio(int fd)
{
  int flags;

  /* If they have O_NONBLOCK, use the Posix way to do it. */
#ifdef O_NONBLOCK
  /* TODO(*): O_NONBLOCK is defined but broken on SunOS 4.1.x and AIX 3.2.5. */
  if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
    flags = 0;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
  /* Otherwise, use the old way of doing it. */
  flags = 1;
  return ioctl(fd, FIOBIO, &flags);
#endif
}


Message* encode(const UPID &from, const UPID &to, const string &name, const string &data = "")
{
  Message* message = new Message();
  message->from = from;
  message->to = to;
  message->name = name;
  message->body = data;
  return message;
}


void transport(Message* message, ProcessBase* sender = NULL)
{
  if (message->to.ip == ip && message->to.port == port) {
    // Local message.
    process_manager->deliver(message, sender);
  } else {
    // Remote message.
    socket_manager->send(message);
  }
}


Message* parse(HttpRequest* request)
{
  if (request->method == "POST" && request->headers.count("User-Agent") > 0) {
    const string& temp = request->headers["User-Agent"];
    const string& libprocess = "libprocess/";
    size_t index = temp.find(libprocess);
    if (index != string::npos) {
      // Okay, now determine 'from'.
      const UPID from(temp.substr(index + libprocess.size(), temp.size()));

      // Now determine 'to'.
      index = request->path.find('/', 1);
      index = index != string::npos ? index - 1 : string::npos;
      const UPID to(request->path.substr(1, index), ip, port);

      // And now determine 'name'.
      index = index != string::npos ? index + 2: request->path.size();
      const string& name = request->path.substr(index);

      VLOG(2) << "Parsed message name '" << name
              << "' for " << to << " from " << from;

      Message* message = new Message();
      message->name = name;
      message->from = from;
      message->to = to;
      message->body = request->body;

      return message;
    }
  }

  return NULL;
}


void handle_async(struct ev_loop* loop, ev_async* _, int revents)
{
  synchronized (watchers) {
    // Start all the new I/O watchers.
    while (!watchers->empty()) {
      ev_io* watcher = watchers->front();
      watchers->pop();
      ev_io_start(loop, watcher);
    }
  }

  synchronized (timeouts) {
    if (update_timer) {
      if (!timeouts->empty()) {
	// Determine when the next timer should fire.
	timeouts_watcher.repeat = timeouts->begin()->first - Clock::now();

        if (timeouts_watcher.repeat <= 0) {
	  // Feed the event now!
	  timeouts_watcher.repeat = 0;
	  ev_timer_again(loop, &timeouts_watcher);
          ev_feed_event(loop, &timeouts_watcher, EV_TIMEOUT);
        } else {
 	  // Don't fire the timer if the clock is paused since we
 	  // don't want time to advance (instead a call to
 	  // clock::advance() will handle the timer).
 	  if (Clock::paused() && timeouts_watcher.repeat > 0) {
 	    timeouts_watcher.repeat = 0;
          }

	  ev_timer_again(loop, &timeouts_watcher);
	}
      }

      update_timer = false;
    }
  }
}


void handle_timeouts(struct ev_loop* loop, ev_timer* _, int revents)
{
  list<timer> timedout;

  synchronized (timeouts) {
    double now = Clock::now();

    VLOG(1) << "Handling timeouts up to "
            << std::fixed << std::setprecision(9) << now;

    foreachkey (double timeout, *timeouts) {
      if (timeout > now) {
        break;
      }

      VLOG(2) << "Have timeout(s) at "
              << std::fixed << std::setprecision(9) << timeout;

      // Record that we have pending timers to execute so the
      // Clock::settle() operation can wait until we're done.
      pending_timers = true;

      foreach (const timer& timer, (*timeouts)[timeout]) {
        timedout.push_back(timer);
      }
    }

    // Now erase the range of timeouts that timed out.
    timeouts->erase(timeouts->begin(), timeouts->upper_bound(now));

    // Okay, so the timeout for the next timer should not have fired.
    CHECK(timeouts->empty() || (timeouts->begin()->first > now));

    // Update the timer as necessary.
    if (!timeouts->empty()) {
      // Determine when the next timer should fire.
      timeouts_watcher.repeat = timeouts->begin()->first - Clock::now();

      if (timeouts_watcher.repeat <= 0) {
        // Feed the event now!
        timeouts_watcher.repeat = 0;
        ev_timer_again(loop, &timeouts_watcher);
        ev_feed_event(loop, &timeouts_watcher, EV_TIMEOUT);
      } else {
        // Don't fire the timer if the clock is paused since we don't
        // want time to advance (instead a call to Clock::advance()
        // will handle the timer).
        if (Clock::paused() && timeouts_watcher.repeat > 0) {
          timeouts_watcher.repeat = 0;
        }

        ev_timer_again(loop, &timeouts_watcher);
      }
    }

    update_timer = false; // Since we might have a queued update_timer.
  }

  // Update current time of process (if it's present/valid). It might
  // be necessary to actually add some more synchronization around
  // this so that, for example, pausing and resuming the clock doesn't
  // cause some processes to get thier current times updated and
  // others not. Since ProcessManager::use acquires the 'processes'
  // lock we had to move this out of the synchronized (timeouts) above
  // since there was a deadlock with acquring 'processes' then
  // 'timeouts' (reverse order) in ProcessManager::cleanup. Note that
  // current time may be greater than the timeout if a local message
  // was received (and happens-before kicks in).
  if (Clock::paused()) {
    foreach (const timer& timer, timedout) {
      if (ProcessReference process = process_manager->use(timer.pid)) {
        Clock::update(process, timer.timeout);
      }
    }
  }

  // Execute the thunks of the timeouts that timed out (TODO(benh): Do
  // this async so that we don't tie up the event thread!).
  foreach (const timer& timer, timedout) {
    timer.thunk();
  }

  // Mark ourselves as done executing the timers since it's now safe
  // for a call to Clock::settle() to check if there will be any
  // future timeouts reached.
  synchronized (timeouts) {
    pending_timers = false;
  }
}


void recv_data(struct ev_loop *loop, ev_io *watcher, int revents)
{
  DataDecoder* decoder = (DataDecoder*) watcher->data;
  
  int c = watcher->fd;

  while (true) {
    const ssize_t size = 80 * 1024;
    ssize_t length = 0;

    char data[size];

    length = recv(c, data, size, 0);

    if (length < 0 && (errno == EINTR)) {
      // Interrupted, try again now.
      continue;
    } else if (length < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      // Might block, try again later.
      break;
    } else if (length <= 0) {
      // Socket error or closed.
      if (length < 0) {
        const char* error = strerror(errno);
        VLOG(2) << "Socket error while receiving: " << error;
      } else {
        VLOG(2) << "Socket closed while receiving";
      }
      socket_manager->closed(c);
      delete decoder;
      ev_io_stop(loop, watcher);
      delete watcher;
      break;
    } else {
      CHECK(length > 0);

      // Decode as much of the data as possible into HTTP requests.
      const deque<HttpRequest*>& requests = decoder->decode(data, length);

      if (!requests.empty()) {
        foreach (HttpRequest* request, requests) {
          process_manager->deliver(c, request);
        }
      } else if (requests.empty() && decoder->failed()) {
        VLOG(2) << "Decoder error while receiving";
        socket_manager->closed(c);
        delete decoder;
        ev_io_stop(loop, watcher);
        delete watcher;
        break;
      }
    }
  }
}


void send_data(struct ev_loop *loop, ev_io *watcher, int revents)
{
  DataEncoder* encoder = (DataEncoder*) watcher->data;

  int c = watcher->fd;

  while (true) {
    const void* data;
    size_t size;

    data = encoder->next(&size);
    CHECK(size > 0);

    ssize_t length = send(c, data, size, MSG_NOSIGNAL);

    if (length < 0 && (errno == EINTR)) {
      // Interrupted, try again now.
      encoder->backup(size);
      continue;
    } else if (length < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      // Might block, try again later.
      encoder->backup(size);
      break;
    } else if (length <= 0) {
      // Socket error or closed.
      if (length < 0) {
        const char* error = strerror(errno);
        VLOG(2) << "Socket error while sending: " << error;
      } else {
        VLOG(2) << "Socket closed while sending";
      }
      socket_manager->closed(c);
      delete encoder;
      ev_io_stop(loop, watcher);
      delete watcher;
      break;
    } else {
      CHECK(length > 0);

      // Update the encoder with the amount sent.
      encoder->backup(size - length);

      // See if there is any more of the message to send.
      if (encoder->remaining() == 0) {
        delete encoder;

        // Check for more stuff to send on socket.
        encoder = socket_manager->next(c);
        if (encoder != NULL) {
          watcher->data = encoder;
        } else {
          // Nothing more to send right now, clean up.
          ev_io_stop(loop, watcher);
          delete watcher;
          break;
        }
      }
    }
  }
}


void sending_connect(struct ev_loop *loop, ev_io *watcher, int revents)
{
  int c = watcher->fd;

  // Now check that a successful connection was made.
  int opt;
  socklen_t optlen = sizeof(opt);

  if (getsockopt(c, SOL_SOCKET, SO_ERROR, &opt, &optlen) < 0 || opt != 0) {
    // Connect failure.
    VLOG(1) << "Socket error while connecting";
    socket_manager->closed(c);
    MessageEncoder* encoder = (MessageEncoder*) watcher->data;
    delete encoder;
    ev_io_stop(loop, watcher);
    delete watcher;
  } else {
    // We're connected! Now let's do some sending.
    ev_io_stop(loop, watcher);
    ev_io_init(watcher, send_data, c, EV_WRITE);
    ev_io_start(loop, watcher);
  }
}


void receiving_connect(struct ev_loop *loop, ev_io *watcher, int revents)
{
  int c = watcher->fd;

  // Now check that a successful connection was made.
  int opt;
  socklen_t optlen = sizeof(opt);

  if (getsockopt(c, SOL_SOCKET, SO_ERROR, &opt, &optlen) < 0 || opt != 0) {
    // Connect failure.
    VLOG(1) << "Socket error while connecting";
    socket_manager->closed(c);
    DataDecoder* decoder = (DataDecoder*) watcher->data;
    delete decoder;
    ev_io_stop(loop, watcher);
    delete watcher;
  } else {
    // We're connected! Now let's do some receiving.
    ev_io_stop(loop, watcher);
    ev_io_init(watcher, recv_data, c, EV_READ);
    ev_io_start(loop, watcher);
  }
}


void accept(struct ev_loop *loop, ev_io *watcher, int revents)
{
  int s = watcher->fd;

  sockaddr_in addr;
  socklen_t addrlen = sizeof(addr);

  int c = ::accept(s, (sockaddr *) &addr, &addrlen);

  if (c < 0) {
    return;
  }

  if (set_nbio(c) < 0) {
    close(c);
    return;
  }

  // Turn off Nagle (via TCP_NODELAY) so pipelined requests don't wait.
  int on = 1;
  if (setsockopt(c, SOL_TCP, TCP_NODELAY, &on, sizeof(on)) < 0) {
    close(c);
  } else {
    // Allocate and initialize the decoder and watcher.
    DataDecoder* decoder = new DataDecoder();

    ev_io *watcher = new ev_io();
    watcher->data = decoder;

    ev_io_init(watcher, recv_data, c, EV_READ);
    ev_io_start(loop, watcher);
  }
}


void* serve(void* arg)
{
  ev_loop(((struct ev_loop*) arg), 0);

  return NULL;
}


void* schedule(void* arg)
{
  __process__ = NULL; // Start off not running anything.

  do {
    ProcessBase* process = process_manager->dequeue();
    if (process == NULL) {
      Gate::state_t old = gate->approach();
      process = process_manager->dequeue();
      if (process == NULL) {
	gate->arrive(old); // Wait at gate if idle.
	continue;
      } else {
	gate->leave();
      }
    }
    process_manager->resume(process);
  } while (true);
}


// We might find value in catching terminating signals at some point.
// However, for now, adding signal handlers freely is not allowed
// because they will clash with Java and Python virtual machines and
// causes hard to debug crashes/segfaults.

// void sigbad(int signal, struct sigcontext *ctx)
// {
//   // Pass on the signal (so that a core file is produced).
//   struct sigaction sa;
//   sa.sa_handler = SIG_DFL;
//   sigemptyset(&sa.sa_mask);
//   sa.sa_flags = 0;
//   sigaction(signal, &sa, NULL);
//   raise(signal);
// }


void initialize(bool initialize_google_logging)
{
//   static pthread_once_t init = PTHREAD_ONCE_INIT;
//   pthread_once(&init, ...);

  static volatile bool initialized = false;
  static volatile bool initializing = true;

  // Try and do the initialization or wait for it to complete.
  if (initialized && !initializing) {
    return;
  } else if (initialized && initializing) {
    while (initializing);
    return;
  } else {
    if (!__sync_bool_compare_and_swap(&initialized, false, true)) {
      while (initializing);
      return;
    }
  }

  if (initialize_google_logging) {
    google::InitGoogleLogging("libprocess");
    google::LogToStderr();
  }

//   // Install signal handler.
//   struct sigaction sa;

//   sa.sa_handler = (void (*) (int)) sigbad;
//   sigemptyset (&sa.sa_mask);
//   sa.sa_flags = SA_RESTART;

//   sigaction (SIGTERM, &sa, NULL);
//   sigaction (SIGINT, &sa, NULL);
//   sigaction (SIGQUIT, &sa, NULL);
//   sigaction (SIGSEGV, &sa, NULL);
//   sigaction (SIGILL, &sa, NULL);
// #ifdef SIGBUS
//   sigaction (SIGBUS, &sa, NULL);
// #endif
// #ifdef SIGSTKFLT
//   sigaction (SIGSTKFLT, &sa, NULL);
// #endif
//   sigaction (SIGABRT, &sa, NULL);

//   sigaction (SIGFPE, &sa, NULL);

#ifdef __sun__
  /* Need to ignore this since we can't do MSG_NOSIGNAL on Solaris. */
  signal(SIGPIPE, SIG_IGN);
#endif // __sun__

  // Create a new ProcessManager and SocketManager.
  process_manager = new ProcessManager();
  socket_manager = new SocketManager();

  // Setup the thread local process pointer.
  pthread_key_t key;
  if (pthread_key_create(&key, NULL) != 0) {
    LOG(FATAL) << "Failed to initialize, pthread_key_create";
  }

  _process_ = new ThreadLocal<ProcessBase>(key);

  // Setup processing threads.
  for (int i = 0; i < NUMBER_OF_PROCESSING_THREADS; i++) {
    pthread_t thread; // For now, not saving handles on our threads.
    if (pthread_create(&thread, NULL, schedule, NULL) != 0) {
      LOG(FATAL) << "Failed to initialize, pthread_create";
    }
  }

  ip = 0;
  port = 0;

  char *value;

  // Check environment for ip.
  value = getenv("LIBPROCESS_IP");
  if (value != NULL) {
    int result = inet_pton(AF_INET, value, &ip);
    if (result == 0) {
      LOG(FATAL) << "LIBPROCESS_IP=" << value << " was unparseable";
    } else if (result < 0) {
      PLOG(FATAL) << "Failed to initialize, inet_pton";
    }
  }

  // Check environment for port.
  value = getenv("LIBPROCESS_PORT");
  if (value != NULL) {
    int result = atoi(value);
    if (result < 0 || result > USHRT_MAX) {
      LOG(FATAL) << "LIBPROCESS_PORT=" << value << " is not a valid port";
    }
    port = result;
  }

  // Create a "server" socket for communicating with other nodes.
  if ((s = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) < 0) {
    PLOG(FATAL) << "Failed to initialize, socket";
  }

  // Make socket non-blocking.
  if (set_nbio(s) < 0) {
    PLOG(FATAL) << "Failed to initialize, set_nbio";
  }

  // Allow address reuse.
  int on = 1;
  if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
    PLOG(FATAL) << "Failed to initialize, setsockopt(SO_REUSEADDR)";
  }

  // Set up socket.
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = PF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (bind(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
    PLOG(FATAL) << "Failed to initialize, bind";
  }

  // Lookup and store assigned ip and assigned port.
  socklen_t addrlen = sizeof(addr);
  if (getsockname(s, (struct sockaddr *) &addr, &addrlen) < 0) {
    PLOG(FATAL) << "Failed to initialize, getsockname";
  }

  ip = addr.sin_addr.s_addr;
  port = ntohs(addr.sin_port);

  // Lookup hostname if missing ip or if ip is 127.0.0.1 in case we
  // actually have a valid external ip address. Note that we need only
  // one ip address, so that other processes can send and receive and
  // don't get confused as to whom they are sending to.
  if (ip == 0 || ip == 2130706433) {
    char hostname[512];

    if (gethostname(hostname, sizeof(hostname)) < 0) {
      PLOG(FATAL) << "Ffailed to initialize, gethostname";
    }

    // Lookup IP address of local hostname.
    struct hostent* he;

    if ((he = gethostbyname2(hostname, AF_INET)) == NULL) {
      PLOG(FATAL) << "Failed to initialize, gethostbyname2";
    }

    ip = *((uint32_t *) he->h_addr_list[0]);
  }

  if (listen(s, 500000) < 0) {
    PLOG(FATAL) << "Failed to initialize, listen";
  }

  // Setup event loop.
#ifdef __sun__
  loop = ev_default_loop(EVBACKEND_POLL | EVBACKEND_SELECT);
#else
  loop = ev_default_loop(EVFLAG_AUTO);
#endif // __sun__

  ev_async_init(&async_watcher, handle_async);
  ev_async_start(loop, &async_watcher);

  ev_timer_init(&timeouts_watcher, handle_timeouts, 0., 2100000.0);
  ev_timer_again(loop, &timeouts_watcher);

  ev_io_init(&server_watcher, accept, s, EV_READ);
  ev_io_start(loop, &server_watcher);

//   ev_child_init(&child_watcher, child_exited, pid, 0);
//   ev_child_start(loop, &cw);

//   /* Install signal handler. */
//   struct sigaction sa;

//   sa.sa_handler = ev_sighandler;
//   sigfillset (&sa.sa_mask);
//   sa.sa_flags = SA_RESTART; /* if restarting works we save one iteration */
//   sigaction (w->signum, &sa, 0);

//   sigemptyset (&sa.sa_mask);
//   sigaddset (&sa.sa_mask, w->signum);
//   sigprocmask (SIG_UNBLOCK, &sa.sa_mask, 0);

  pthread_t thread; // For now, not saving handles on our threads.
  if (pthread_create(&thread, NULL, serve, loop) != 0) {
    LOG(FATAL) << "Failed to initialize, pthread_create";
  }

  // Need to set initialzing here so that we can actually invoke
  // 'spawn' below for the garbage collector.
  initializing = false;

  // Create global garbage collector.
  gc = spawn(new GarbageCollector());

  char temp[INET_ADDRSTRLEN];
  if (inet_ntop(AF_INET, (in_addr *) &ip, temp, INET_ADDRSTRLEN) == NULL) {
    PLOG(FATAL) << "Failed to initialize, inet_ntop";
  }

  VLOG(1) << "libprocess is initialized on " << temp << ":" << port;
}


HttpResponseWaiter::HttpResponseWaiter(const PID<HttpProxy>& _proxy,
                                       Future<HttpResponse>* _future,
                                       bool _persist)
  : proxy(_proxy), future(_future), persist(_persist)
{
  // Wait for any event on the future.
  deferred<void(const Future<HttpResponse>&)> waited = executor.defer(
      lambda::bind(&HttpResponseWaiter::waited, this, lambda::_1));

  future->onAny(waited);

  // Also create a timer so we don't wait forever.
  deferred<void(void)> timeout = executor.defer(
      lambda::bind(&HttpResponseWaiter::timeout, this));

  timers::create(30, timeout);
}


void HttpResponseWaiter::waited(const Future<HttpResponse>&)
{
  if (future->isReady()) {
    process::dispatch(proxy, &HttpProxy::ready, future, persist);
  } else {
    // TODO(benh): Consider handling other "states" of future
    // (discarded, failed, etc) with different HTTP statuses.
    process::dispatch(proxy, &HttpProxy::unavailable, future, persist);
  }

  executor.stop(); // Ensure we ignore the timeout.
}


void HttpResponseWaiter::timeout()
{
  process::dispatch(proxy, &HttpProxy::unavailable, future, persist);

  executor.stop(); // Ensure we ignore the future.
}


HttpProxy::HttpProxy(int _c) : c(_c) {}


void HttpProxy::handle(Future<HttpResponse>* future, bool persist)
{
  HttpResponseWaiter* waiter = new HttpResponseWaiter(this, future, persist);
  waiters[future] = waiter;
}


void HttpProxy::ready(Future<HttpResponse>* future, bool persist)
{
  CHECK(waiters.count(future) > 0);
  HttpResponseWaiter* waiter = waiters[future];
  waiters.erase(future);
  delete waiter;

  CHECK(future->isReady());

  const HttpResponse& response = future->get();

  // Don't persist the connection if the responder doesn't want it to.
  if (response.headers.count("Connection") > 0) {
    const string& connection = response.headers.find("Connection")->second;
    if (connection == "close") {
      persist = false;
    }
  }

  HttpResponseEncoder* encoder =
    new HttpResponseEncoder(response);

  delete future;

  // See the semantics of SocketManager::send for details about how
  // the socket will get closed (it might actually already be closed
  // before we issue this send).
  socket_manager->send(encoder, c, persist);
}


void HttpProxy::unavailable(Future<HttpResponse>* future, bool persist)
{
  CHECK(waiters.count(future) > 0);
  HttpResponseWaiter* waiter = waiters[future];
  waiters.erase(future);
  delete waiter;

  HttpResponseEncoder* encoder =
    new HttpResponseEncoder(HttpServiceUnavailableResponse());

  delete future;

  // As above, the socket might all ready be closed when we do a send.
  socket_manager->send(encoder, c, persist);
}


SocketManager::SocketManager()
{
  synchronizer(this) = SYNCHRONIZED_INITIALIZER_RECURSIVE;
}


SocketManager::~SocketManager() {}


void SocketManager::link(ProcessBase *process, const UPID &to)
{
  // TODO(benh): The semantics we want to support for link are such
  // that if there is nobody to link to (local or remote) then an
  // ExitedEvent gets generated. This functionality has only been
  // implemented when the link is local, not remote. Of course, if
  // there is nobody listening on the remote side, then this should
  // work remotely ... but if there is someone listening remotely just
  // not at that id, then it will silently continue executing.

  CHECK(process != NULL);

  Node node(to.ip, to.port);

  synchronized (this) {
    // Check if node is remote and there isn't a persistant link.
    if ((node.ip != ip || node.port != port) && persists.count(node) == 0) {
      // Okay, no link, lets create a socket.
      int s;

      if ((s = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) < 0) {
        PLOG(FATAL) << "Failed to link, socket";
      }

      if (set_nbio(s) < 0) {
        PLOG(FATAL) << "Failed to link, set_nbio";
      }

      sockets[s] = node;

      persists[node] = s;

      sockaddr_in addr;
      memset(&addr, 0, sizeof(addr));
      addr.sin_family = PF_INET;
      addr.sin_port = htons(to.port);
      addr.sin_addr.s_addr = to.ip;

      // Allocate and initialize the decoder and watcher.
      DataDecoder* decoder = new DataDecoder();

      ev_io *watcher = new ev_io();
      watcher->data = decoder;

      // Try and connect to the node using this socket.
      if (connect(s, (sockaddr *) &addr, sizeof(addr)) < 0) {
        if (errno != EINPROGRESS) {
          PLOG(FATAL) << "Failed to link, connect";
        }

        // Wait for socket to be connected.
        ev_io_init(watcher, receiving_connect, s, EV_WRITE);
      } else {
        ev_io_init(watcher, recv_data, s, EV_READ);
      }

      // Enqueue the watcher.
      synchronized (watchers) {
        watchers->push(watcher);
      }

      // Interrupt the loop.
      ev_async_send(loop, &async_watcher);
    }

    links[to].insert(process);
  }
}


PID<HttpProxy> SocketManager::proxy(int s)
{
  synchronized (this) {
    if (sockets.count(s) > 0) {
      CHECK(proxies.count(s) > 0);
      return proxies[s]->self();
    } else {
      // Register the socket with the manager for sending purposes. The
      // current design doesn't let us create a valid "node" for this
      // socket, so we use a "default" one for now.
      sockets[s] = Node();

      CHECK(proxies.count(s) == 0);

      HttpProxy* proxy = new HttpProxy(s);
      spawn(proxy, true);
      proxies[s] = proxy;
      return proxy->self();
    }
  }
}


void SocketManager::send(DataEncoder* encoder, int s, bool persist)
{
  CHECK(encoder != NULL);

  // TODO(benh): The current mechanism here is insufficient. It could
  // be the case that an HttpProxy attempts to do a send on a socket
  // just as that socket has been closed and then re-opened for
  // another connection. In this case, the data sent on that socket
  // will be completely bogus ... one easy fix would be to check the
  // proxy that is associated with the socket to eliminate this race.

  synchronized (this) {
    if (sockets.count(s) > 0) {
      if (outgoing.count(s) > 0) {
        outgoing[s].push(encoder);
      } else {
        // Initialize the outgoing queue.
        outgoing[s];

        // Allocate and initialize the watcher.
        ev_io *watcher = new ev_io();
        watcher->data = encoder;

        ev_io_init(watcher, send_data, s, EV_WRITE);

        synchronized (watchers) {
          watchers->push(watcher);
        }

        ev_async_send(loop, &async_watcher);
      }

      // Set the socket to get closed if not persistant.
      if (!persist) {
        disposables.insert(s);
      }
    } else {
      VLOG(1) << "Attempting to send on a no longer valid socket!";
      delete encoder;
    }
  }
}


void SocketManager::send(Message* message)
{
  CHECK(message != NULL);

  DataEncoder* encoder = new MessageEncoder(message);

  Node node(message->to.ip, message->to.port);

  synchronized (this) {
    // Check if there is already a socket.
    bool persistant = persists.count(node) > 0;
    bool temporary = temps.count(node) > 0;
    if (persistant || temporary) {
      int s = persistant ? persists[node] : temps[node];
      send(encoder, s, persistant);
    } else {
      // No peristant or temporary socket to the node currently
      // exists, so we create a temporary one.
      int s;

      if ((s = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) < 0) {
        PLOG(FATAL) << "Failed to send, socket";
      }

      if (set_nbio(s) < 0) {
        PLOG(FATAL) << "Failed to send, set_nbio";
      }

      sockets[s] = node;

      temps[node] = s;
      disposables.insert(s);

      // Initialize the outgoing queue.
      outgoing[s];

      // Try and connect to the node using this socket.
      sockaddr_in addr;
      memset(&addr, 0, sizeof(addr));
      addr.sin_family = PF_INET;
      addr.sin_port = htons(message->to.port);
      addr.sin_addr.s_addr = message->to.ip;

      // Allocate and initialize the watcher.
      ev_io *watcher = new ev_io();
      watcher->data = encoder;
    
      if (connect(s, (sockaddr *) &addr, sizeof(addr)) < 0) {
        if (errno != EINPROGRESS) {
          PLOG(FATAL) << "Failed to send, connect";
        }

        // Initialize watcher for connecting.
        ev_io_init(watcher, sending_connect, s, EV_WRITE);
      } else {
        // Initialize watcher for sending.
        ev_io_init(watcher, send_data, s, EV_WRITE);
      }

      // Enqueue the watcher.
      synchronized (watchers) {
        watchers->push(watcher);
      }

      ev_async_send(loop, &async_watcher);
    }
  }
}


DataEncoder* SocketManager::next(int s)
{
  DataEncoder* encoder = NULL;

  synchronized (this) {
    CHECK(sockets.count(s) > 0);
    CHECK(outgoing.count(s) > 0);

    if (!outgoing[s].empty()) {
      // More messages!
      encoder = outgoing[s].front();
      outgoing[s].pop();
    } else {
      // No more messages ... erase the outgoing queue.
      outgoing.erase(s);

      // Close the socket if it was set for disposal.
      if (disposables.count(s) > 0) {
        // Also try and remove from temps.
        const Node& node = sockets[s];
        if (temps.count(node) > 0 && temps[node] == s) {
          temps.erase(node);
        } else if (proxies.count(s) > 0) {
          HttpProxy* proxy = proxies[s];
          proxies.erase(s);
          terminate(proxy);
        }

        disposables.erase(s);
        sockets.erase(s);
        close(s);
      }
    }
  }

  return encoder;
}


void SocketManager::closed(int s)
{
  HttpProxy* proxy = NULL; // Non-null if needs to be terminated.

  synchronized (this) {
    if (sockets.count(s) > 0) {
      const Node& node = sockets[s];

      // Don't bother invoking exited unless socket was persistant.
      if (persists.count(node) > 0 && persists[node] == s) {
	persists.erase(node);
        exited(node);
      } else if (temps.count(node) > 0 && temps[node] == s) {
        temps.erase(node);
      } else if (proxies.count(s) > 0) {
        proxy = proxies[s];
        proxies.erase(s);
      }

      outgoing.erase(s);
      disposables.erase(s);
      sockets.erase(s);
    }
  }

  // We terminate the proxy outside the synchronized block to avoid
  // possible deadlock between the ProcessManager and SocketManager.
  if (proxy != NULL) {
    terminate(proxy);
  }

  // This might have just been a receiving socket (only sending
  // sockets, with the exception of the receiving side of a persistant
  // socket, get added to 'sockets'), so we want to make sure to call
  // close so that the file descriptor can get reused.
  close(s);
}


void SocketManager::exited(const Node& node)
{
  // TODO(benh): It would be cleaner if this routine could call back
  // into ProcessManager ... then we wouldn't have to convince
  // ourselves that the accesses to each Process object will always be
  // valid.
  synchronized (this) {
    list<UPID> removed;
    // Look up all linked processes.
    foreachpair (const UPID& linkee, set<ProcessBase*>& processes, links) {
      if (linkee.ip == node.ip && linkee.port == node.port) {
        foreach (ProcessBase* linker, processes) {
          linker->enqueue(new ExitedEvent(linkee));
        }
        removed.push_back(linkee);
      }
    }

    foreach (const UPID &pid, removed) {
      links.erase(pid);
    }
  }
}


void SocketManager::exited(ProcessBase* process)
{
  // An exited event is enough to cause the process to get deleted
  // (e.g., by the garbage collector), which means we can't
  // dereference process (or even use the address) after we enqueue at
  // least one exited event. Thus, we save the process pid.
  const UPID pid = process->pid;

  // Likewise, we need to save the current time of the process so we
  // can update the clocks of linked processes as appropriate.
  const double secs = Clock::now(process);

  synchronized (this) {
    // Iterate through the links, removing any links the process might
    // have had and creating exited events for any linked processes.
    foreachpair (const UPID& linkee, set<ProcessBase*>& processes, links) {
      processes.erase(process);

      if (linkee == pid) {
        foreach (ProcessBase* linker, processes) {
          CHECK(linker != process) << "Process linked with itself";
          synchronized (timeouts) {
            if (Clock::paused()) {
              Clock::update(linker, secs);
            }
          }
          linker->enqueue(new ExitedEvent(linkee));
        }
      }
    }

    links.erase(pid);
  }
}


ProcessManager::ProcessManager()
{
  synchronizer(processes) = SYNCHRONIZED_INITIALIZER;
  synchronizer(runq) = SYNCHRONIZED_INITIALIZER;
  running = 0;
  __sync_synchronize(); // Ensure write to 'running' visible in other threads.
}


ProcessManager::~ProcessManager() {}


ProcessReference ProcessManager::use(const UPID &pid)
{
  if (pid.ip == ip && pid.port == port) {
    synchronized (processes) {
      if (processes.count(pid.id) > 0) {
        // Note that the ProcessReference constructor _must_ get
        // called while holding the lock on processes so that waiting
        // for references is atomic (i.e., race free).
        return ProcessReference(processes[pid.id]);
      }
    }
  }

  return ProcessReference(NULL);
}


bool ProcessManager::deliver(Message* message, ProcessBase* sender)
{
  CHECK(message != NULL);

  if (ProcessReference receiver = use(message->to)) {
    // If we have a local sender AND we are using a manual clock
    // then update the current time of the receiver to preserve
    // the happens-before relationship between the sender and
    // receiver. Note that the assumption is that the sender
    // remains valid for at least the duration of this routine (so
    // that we can look up it's current time).
    if (sender != NULL) {
      synchronized (timeouts) {
        if (Clock::paused()) {
          Clock::order(sender, receiver);
        }
      }
    }

    receiver->enqueue(new MessageEvent(message));
  } else {
    delete message;
    return false;
  }

  return true;
}


// TODO(benh): Refactor and share code with above!
bool ProcessManager::deliver(int c, HttpRequest* request, ProcessBase* sender)
{
  CHECK(request != NULL);

  // Determine whether or not this is a libprocess message.
  Message* message = parse(request);

  if (message != NULL) {
    delete request;
    return deliver(message, sender);
  }

  // Treat this as an HTTP request and check for a valid receiver.
  string path = request->path.substr(1, request->path.find('/', 1) - 1);

  UPID to(path, ip, port);

  if (ProcessReference receiver = use(to)) {
    // If we have a local sender AND we are using a manual clock
    // then update the current time of the receiver to preserve
    // the happens-before relationship between the sender and
    // receiver. Note that the assumption is that the sender
    // remains valid for at least the duration of this routine (so
    // that we can look up it's current time).
    if (sender != NULL) {
      synchronized (timeouts) {
        if (Clock::paused()) {
          Clock::order(sender, receiver);
        }
      }
    }

    // Enqueue the event.
    receiver->enqueue(new HttpEvent(c, request));
  } else {
    // This has no receiver, send error response.
    VLOG(1) << "Returning '404 Not Found' for '" << request->path << "'";

    HttpResponseEncoder* encoder =
      new HttpResponseEncoder(HttpNotFoundResponse());

    // TODO(benh): Socket might be closed and then re-opened!
    socket_manager->send(encoder, c, request->keepAlive);

    // Cleanup request.
    delete request;
    return false;
  }

  return true;
}


// TODO(benh): Refactor and share code with above!
bool ProcessManager::deliver(
    const UPID& to,
    lambda::function<void(ProcessBase*)>* f,
    ProcessBase* sender)
{
  CHECK(f != NULL);

  if (ProcessReference receiver = use(to)) {
    // If we have a local sender AND we are using a manual clock
    // then update the current time of the receiver to preserve
    // the happens-before relationship between the sender and
    // receiver. Note that the assumption is that the sender
    // remains valid for at least the duration of this routine (so
    // that we can look up it's current time).
    if (sender != NULL) {
      synchronized (timeouts) {
        if (Clock::paused()) {
          Clock::order(sender, receiver);
        }
      }
    }

    receiver->enqueue(new DispatchEvent(f));
  } else {
    delete f;
    return false;
  }

  return true;
}


UPID ProcessManager::spawn(ProcessBase* process, bool manage)
{
  CHECK(process != NULL);

  synchronized (processes) {
    if (processes.count(process->pid.id) > 0) {
      return UPID();
    } else {
      processes[process->pid.id] = process;
    }
  }

  // Use the garbage collector if requested.
  if (manage) {
    dispatch(gc, &GarbageCollector::manage<ProcessBase>, process);
  }

  // Add process to the run queue (so 'initialize' will get invoked).
  enqueue(process);

  VLOG(2) << "Spawned process " << process->self();

  return process->self();
}


void ProcessManager::resume(ProcessBase* process)
{
  __process__ = process;

  VLOG(2) << "Resuming " << process->pid << " at "
          << std::fixed << std::setprecision(9) << Clock::now();

  bool terminate = false;
  bool blocked = false;

  CHECK(process->state == ProcessBase::BOTTOM ||
        process->state == ProcessBase::READY);

  if (process->state == ProcessBase::BOTTOM) {
    process->state = ProcessBase::RUNNING;
    try { process->initialize(); }
    catch (...) { terminate = true; }
  }

  while (!terminate && !blocked) {
    Event* event = NULL;

    process->lock();
    {
      if (process->events.size() > 0) {
        event = process->events.front();
        process->events.pop_front();
        process->state = ProcessBase::RUNNING;
      } else {
        process->state = ProcessBase::BLOCKED;
        blocked = true;
      }
    }
    process->unlock();

    if (!blocked) {
      CHECK(event != NULL);

      // Determine if we should terminate.
      terminate = event->is<TerminateEvent>();

      // Now service the event.
      try {
        process->serve(*event);
      } catch (const std::exception& e) {
        std::cerr << "libprocess: " << process->pid
                  << " terminating due to "
                  << e.what() << std::endl;
        terminate = true;
      } catch (...) {
        std::cerr << "libprocess: " << process->pid
                  << " terminating due to unknown exception" << std::endl;
        terminate = true;
      }

      delete event;

      if (terminate) {
        cleanup(process);
      }
    }
  }

  __process__ = NULL;

  CHECK_GE(running, 1);
  __sync_fetch_and_sub(&running, 1);
}


void ProcessManager::cleanup(ProcessBase* process)
{
  VLOG(2) << "Cleaning up " << process->pid;

  // Processes that were waiting on exiting process.
  list<ProcessBase*> resumable;

  // Possible gate non-libprocess threads are waiting at.
  Gate* gate = NULL;
 
  // Remove process.
  synchronized (processes) {
    // Wait for all process references to get cleaned up.
    while (process->refs > 0) {
      asm ("pause");
      __sync_synchronize();
    }

    process->lock();
    {
      // Free any pending events.
      while (!process->events.empty()) {
        Event* event = process->events.front();
        process->events.pop_front();
        delete event;
      }

      processes.erase(process->pid.id);
 
      // Lookup gate to wake up waiting threads.
      map<ProcessBase*, Gate*>::iterator it = gates.find(process);
      if (it != gates.end()) {
        gate = it->second;
        // N.B. The last thread that leaves the gate also free's it.
        gates.erase(it);
      }

      CHECK(process->refs == 0);
      process->state = ProcessBase::FINISHED;
    }
    process->unlock();

    // Note that we don't remove the process from the clock during
    // cleanup, but rather the clock is reset for a process when it is
    // created (see ProcessBase::ProcessBase). We do this so that
    // SocketManager::exited can access the current time of the
    // process to "order" exited events. It might make sense to
    // consider storing the time of the process as a field of the
    // class instead.

    // Now we tell the socket manager about this process exiting so
    // that it can create exited events for linked processes. We
    // _must_ do this while synchronized on processes because
    // otherwise another process could attempt to link this process
    // and SocketManger::link would see that the processes doesn't
    // exist when it attempts to get a ProcessReference (since we
    // removed the process above) thus causing an exited event, which
    // could cause the process to get deleted (e.g., the garbage
    // collector might link _after_ the process has already been
    // removed, thus getting an exited event but we don't want that
    // exited event to fire until after we have used the process in
    // SocketManager::exited.
    socket_manager->exited(process);
  }

  // Confirm process not in runq.
  synchronized (runq) {
    CHECK(find(runq.begin(), runq.end(), process) == runq.end());
  }

  // ***************************************************************
  // At this point we can no longer dereference the process since it
  // might already be deallocated (e.g., by the garbage collector).
  // ***************************************************************

  if (gate != NULL) {
    gate->open();
  }
}


void ProcessManager::link(ProcessBase* process, const UPID& to)
{
  // Check if the pid is local.
  if (!(to.ip == ip && to.port == port)) {
    socket_manager->link(process, to);
  } else {
    // Since the pid is local we want to get a reference to it's
    // underlying process so that while we are invoking the link
    // manager we don't miss sending a possible ExitedEvent.
    if (ProcessReference _ = use(to)) {
      socket_manager->link(process, to);
    } else {
      // Since the pid isn't valid it's process must have already died
      // (or hasn't been spawned yet) so send a process exit message.
      process->enqueue(new ExitedEvent(to));
    }
  }
}


void ProcessManager::terminate(
    const UPID& pid,
    bool inject,
    ProcessBase* sender)
{
  if (ProcessReference process = use(pid)) {
    if (sender != NULL) {
      synchronized (timeouts) {
        if (Clock::paused()) {
          Clock::order(sender, process);
        }
      }

      process->enqueue(new TerminateEvent(sender->self()), inject);
    } else {
      process->enqueue(new TerminateEvent(UPID()), inject);
    }
  }
}


bool ProcessManager::wait(const UPID& pid)
{
  // We use a gate for waiters. A gate is single use. That is, a new
  // gate is created when the first thread shows up and wants to wait
  // for a process that currently has no gate. Once that process
  // exits, the last thread to leave the gate will also clean it
  // up. Note that a gate will never get more threads waiting on it
  // after it has been opened, since the process should no longer be
  // valid and therefore will not have an entry in 'processes'.

  Gate* gate = NULL;
  Gate::state_t old;

  ProcessBase* process = NULL; // Set to non-null if we donate thread.

  // Try and approach the gate if necessary.
  synchronized (processes) {
    if (processes.count(pid.id) > 0) {
      process = processes[pid.id];
      CHECK(process->state != ProcessBase::FINISHED);

      // Check and see if a gate already exists.
      if (gates.find(process) == gates.end()) {
        gates[process] = new Gate();
      }

      gate = gates[process];
      old = gate->approach();

      // Check if it is runnable in order to donate this thread.
      if (process->state == ProcessBase::BOTTOM ||
          process->state == ProcessBase::READY) {
        synchronized (runq) {
          list<ProcessBase*>::iterator it =
            find(runq.begin(), runq.end(), process);
          if (it != runq.end()) {
            runq.erase(it);
          } else {
            // Another thread has resumed the process ...
            process = NULL;
          }
        }
      } else {
        // Process is not runnable, so no need to donate ...
        process = NULL;
      }
    }
  }

  if (process != NULL) {
    VLOG(1) << "Donating thread to " << process->pid << " while waiting";
    ProcessBase* donator = __process__;
    __sync_fetch_and_add(&running, 1);
    process_manager->resume(process);
    __process__ = donator;
  }

  // TODO(benh): Donating only once may not be sufficient, so we might
  // still deadlock here ... perhaps warn if that's the case?

  // Now arrive at the gate and wait until it opens.
  if (gate != NULL) {
    gate->arrive(old);

    if (gate->empty()) {
      delete gate;
    }

    return true;
  }

  return false;
}


void ProcessManager::enqueue(ProcessBase* process)
{
  CHECK(process != NULL);

  // TODO(benh): Check and see if this process has it's own thread. If
  // it does, push it on that threads runq, and wake up that thread if
  // it's not running. Otherwise, check and see which thread this
  // process was last running on, and put it on that threads runq.

  synchronized (runq) {
    CHECK(find(runq.begin(), runq.end(), process) == runq.end());
    runq.push_back(process);
  }
    
  // Wake up the processing thread if necessary.
  gate->open();
}


ProcessBase* ProcessManager::dequeue()
{
  // TODO(benh): Remove a process from this thread's runq. If there
  // are no processes to run, and this is not a dedicated thread, then
  // steal one from another threads runq.

  ProcessBase* process = NULL;

  synchronized (runq) {
    if (!runq.empty()) {
      process = runq.front();
      runq.pop_front();
      // Increment the running count of processes in order to support
      // the Clock::settle() operation (this must be done atomically
      // with removing the process from the runq).
      __sync_fetch_and_add(&running, 1);
    }
  }

  return process;
}


void ProcessManager::settle()
{
  bool done = true;
  do {
    usleep(10000);
    done = true;
    // Hopefully this is the only place we acquire both these locks.
    synchronized (runq) {
      synchronized (timeouts) {
        CHECK(Clock::paused()); // Since another thread could resume the clock!

        if (!runq.empty()) {
          done = false;
        }

        __sync_synchronize(); // Read barrier for 'running'.
        if (running > 0) {
          done = false;
        }

        if (timeouts->size() > 0 &&
            timeouts->begin()->first <= clock::current) {
          done = false;
        }

        if (pending_timers) {
          done = false;
        }
      }
    }
  } while (!done);
}


namespace timers {

timer create(double secs, const lambda::function<void(void)>& thunk)
{
  static long id = 0;

  double timeout = Clock::now() + secs;

  if (__process__ != NULL) {
    synchronized (timeouts) {
      if (Clock::paused()) {
        timeout = Clock::now(__process__) + secs;
      }
    }
  }

  timer timer;
  timer.id = id++;
  timer.timeout = timeout;
  timer.pid = __process__ != NULL ? __process__->self() : UPID();
  timer.thunk = thunk;

  VLOG(2) << "Created a timer for "
          << std::fixed << std::setprecision(9) << timeout;

  // Add the timer.
  synchronized (timeouts) {
    if (timeouts->size() == 0 || timer.timeout < timeouts->begin()->first) {
      // Need to interrupt the loop to update/set timer repeat.
      (*timeouts)[timer.timeout].push_back(timer);
      update_timer = true;
      ev_async_send(loop, &async_watcher);
    } else {
      // Timer repeat is adequate, just add the timeout.
      CHECK(timeouts->size() >= 1);
      (*timeouts)[timer.timeout].push_back(timer);
    }
  }

  return timer;
}


void cancel(const timer& timer)
{
  synchronized (timeouts) {
    // Check if the timeout is still pending, and if so, erase
    // it. In addition, erase an empty list if we just removed the
    // last timeout.
    if (timeouts->count(timer.timeout) > 0) {
      (*timeouts)[timer.timeout].remove(timer);
      if ((*timeouts)[timer.timeout].empty()) {
        timeouts->erase(timer.timeout);
      }
    }
  }
}

} // namespace timeouts {


ProcessBase::ProcessBase(const std::string& _id)
{
  process::initialize();

  state = ProcessBase::BOTTOM;

  pthread_mutex_init(&m, NULL);

  refs = 0;

  // Generate string representation of unique id for process.
  if (_id != "") {
    pid.id = _id;
  } else {
    stringstream out;
    out << __sync_add_and_fetch(&id, 1);
    pid.id = out.str();
  }

  pid.ip = ip;
  pid.port = port;

  // If using a manual clock, try and set current time of process
  // using happens before relationship between creator and createe!
  synchronized (timeouts) {
    if (Clock::paused()) {
      clock::currents->erase(this); // In case the address is reused!
      if (__process__ != NULL) {
        Clock::order(__process__, this);
      }
    }
  }
}


ProcessBase::~ProcessBase() {}


void ProcessBase::enqueue(Event* event, bool inject)
{
  CHECK(event != NULL);

  // TODO(benh): Put filter inside lock statement below so that we can
  // guarantee the order of the messages seen by a filter are the same
  // as the order of messages seen by the process. Right now two
  // different threads might execute the filter code and then enqueue
  // the messages in non-deterministic orderings (i.e., there are two
  // "atomic" blocks, the filter code here and the enqueue code
  // below).
  synchronized (filterer) {
    if (filterer != NULL) {
      bool filter = false;
      struct FilterVisitor : EventVisitor
      {
        FilterVisitor(bool* _filter) : filter(_filter) {}

        virtual void visit(const MessageEvent& event)
        {
          *filter = filterer->filter(event);
        }

        virtual void visit(const DispatchEvent& event)
        {
          *filter = filterer->filter(event);
        }

        virtual void visit(const HttpEvent& event)
        {
          *filter = filterer->filter(event);
        }

        virtual void visit(const ExitedEvent& event)
        {
          *filter = filterer->filter(event);
        }

        bool* filter;
      } visitor(&filter);

      event->visit(&visitor);

      if (filter) {
        delete event;
        return;
      }
    }
  }

  lock();
  {
    if (state != FINISHED) {
      if (!inject) {
        events.push_back(event);
      } else {
        events.push_front(event);
      }

      if (state == BLOCKED) {
        state = READY;
        process_manager->enqueue(this);
      }

      CHECK(state == BOTTOM ||
            state == READY ||
            state == RUNNING);
    }
  }
  unlock();
}


void ProcessBase::inject(const UPID& from, const string& name, const char* data, size_t length)
{
  if (!from)
    return;

  Message* message = encode(from, pid, name, string(data, length));

  enqueue(new MessageEvent(message), true);
}


void ProcessBase::send(const UPID& to, const string& name, const char* data, size_t length)
{
  if (!to) {
    return;
  }

  // Encode and transport outgoing message.
  transport(encode(pid, to, name, string(data, length)), this);
}


void ProcessBase::visit(const MessageEvent& event)
{
  if (handlers.message.count(event.message->name) > 0) {
    handlers.message[event.message->name](
        event.message->from,
        event.message->body);
  } else if (delegates.count(event.message->name) > 0) {
    VLOG(1) << "Delegating message '" << event.message->name
            << "' to " << delegates[event.message->name];
    Message* message = new Message(*event.message);
    message->to = delegates[event.message->name];
    transport(message, this);
  }
}


void ProcessBase::visit(const DispatchEvent& event)
{
  (*event.function)(this);
}


void ProcessBase::visit(const HttpEvent& event)
{
  // Determine the request "name" (i.e., path).
  size_t index = event.request->path.find('/', 1);

  // Only want the last part of the path (e.g., "foo" in /path/to/foo).
  index = index != string::npos ? index + 1 : event.request->path.size();

  const string& name = event.request->path.substr(index);

  if (handlers.http.count(name) > 0) {
    // Create the promise to link with whatever gets returned, as well
    // as a future to wait for the response.
    std::tr1::shared_ptr<Promise<HttpResponse> > promise(
        new Promise<HttpResponse>());

    Future<HttpResponse>* future = new Future<HttpResponse>(promise->future());

    // Get the HttpProxy pid for this socket.
    PID<HttpProxy> proxy = socket_manager->proxy(event.c);

    // Let the HttpProxy know about this request (via the future).
    dispatch(proxy, &HttpProxy::handle, future, event.request->keepAlive);

    // Finally, call the handler and associate the response with the promise.
    internal::associate(handlers.http[name](*event.request), promise);
  } else {
    VLOG(1) << "Returning '404 Not Found' for '" << event.request->path << "'";

    HttpResponseEncoder* encoder =
      new HttpResponseEncoder(HttpNotFoundResponse());

    // TODO(benh): Socket might be closed and then re-opened!
    socket_manager->send(encoder, event.c, event.request->keepAlive);
  }
}


void ProcessBase::visit(const ExitedEvent& event)
{
  exited(event.pid);
}


void ProcessBase::visit(const TerminateEvent& event)
{
  finalize();
}


UPID ProcessBase::link(const UPID& to)
{
  if (!to) {
    return to;
  }

  process_manager->link(this, to);

  return to;
}


UPID spawn(ProcessBase* process, bool manage)
{
  process::initialize();

  if (process != NULL) {
    // If using a manual clock, try and set current time of process
    // using happens before relationship between spawner and spawnee!
    synchronized (timeouts) {
      if (Clock::paused()) {
        if (__process__ != NULL) {
          Clock::order(__process__, process);
        } else {
          Clock::update(process, Clock::now());
        }
      }
    }

    return process_manager->spawn(process, manage);
  } else {
    return UPID();
  }
}


void terminate(const UPID& pid, bool inject)
{
  process_manager->terminate(pid, inject, __process__);
}


class WaitWaiter : public Process<WaitWaiter>
{
public:
  WaitWaiter(const UPID& _pid, double _secs, bool* _waited)
    : pid(_pid), secs(_secs), waited(_waited) {}

  virtual void initialize()
  {
    VLOG(2) << "Running waiter process for " << pid;
    link(pid);
    delay(secs, self(), &WaitWaiter::timeout);
  }

private:
  virtual void exited(const UPID&)
  {
    VLOG(2) << "Waiter process waited for " << pid;
    *waited = true;
    terminate(self());
  }

  void timeout()
  {
    VLOG(2) << "Waiter process timed out waiting for " << pid;
    *waited = false;
    terminate(self());
  }

private:
  const UPID pid;
  const double secs;
  bool* const waited;
};


bool wait(const UPID& pid, double secs)
{
  process::initialize();

  if (!pid) {
    return false;
  }

  // This could result in a deadlock if some code decides to wait on a
  // process that has invoked that code!
  if (__process__ != NULL && __process__->self() == pid) {
    std::cerr << "\n**** DEADLOCK DETECTED! ****\nYou are waiting on process "
              << pid << " that it is currently executing." << std::endl;
  }

  if (secs == 0) {
    return process_manager->wait(pid);
  }

  bool waited = false;

  WaitWaiter waiter(pid, secs, &waited);
  spawn(waiter);
  wait(waiter);

  return waited;
}


void filter(Filter *filter)
{
  process::initialize();

  synchronized (filterer) {
    filterer = filter;
  }
}


void post(const UPID& to, const string& name, const char* data, size_t length)
{
  process::initialize();

  if (!to) {
    return;
  }

  // Encode and transport outgoing message.
  transport(encode(UPID(), to, name, string(data, length)));
}


namespace internal {

void dispatch(const UPID& pid, lambda::function<void(ProcessBase*)>* f)
{
  process::initialize();

  process_manager->deliver(pid, f, __process__);
}

} // namespace internal {
} // namespace process {
