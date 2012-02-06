#ifdef MESOS_WEBUI

#include <Python.h>

#include <tr1/functional>

#include "common/strings.hpp"
#include "common/thread.hpp"
#include "common/utils.hpp"
#include "common/webui_utils.hpp"

namespace mesos {
namespace internal {
namespace utils {
namespace webui {

static void run(const std::string& directory,
                const std::string& script,
                const std::vector<std::string>& args)
{
  // Setup the Python interpreter and load the script.
  std::string path = directory + "/" + script;

  Py_Initialize();

  // Setup argv for Python interpreter.
  char** argv = new char*[args.size() + 1];

  argv[0] = const_cast<char*>(path.c_str());

  for (int i = 0; i < args.size(); i++) {
    argv[i + 1] = const_cast<char*>(args[i].c_str());
  }

  PySys_SetArgv(args.size() + 1, argv);

  // Run some code to setup PATH and add webui_dir as a variable.
  std::string code =
    "import sys\n"
    "sys.path.append('" + directory + "/common')\n"
    "sys.path.append('" + directory + "/bottle-0.8.3')\n";

  PyRun_SimpleString(code.c_str());

  LOG(INFO) << "Loading webui script at '" << path << "'";

  FILE* file = fopen(path.c_str(), "r");
  PyRun_SimpleFile(file, path.c_str());
  fclose(file);

  Py_Finalize();

  delete[] argv;
}


void start(const Configuration& conf,
           const std::string& script,
           const std::vector<std::string>& args)
{
  // Use either a directory specified via configuration options (which
  // is necessary for running out of the build directory before 'make
  // install') or the directory determined at build time via the
  // preprocessor macro '-DMESOS_WEBUI_DIR' set in the Makefile.
  std::string directory = conf.get("webui_dir", MESOS_WEBUI_DIR);

  // Remove any trailing '/' in directory.
  directory = strings::remove(directory, "/", strings::SUFFIX);

  // Make sure script is a relative path.
  CHECK(script[0] != '/')
    << "Expecting relative path for webui script (relative to 'webui_dir')";

  // Make sure directory/script exists.
  std::string path = directory + "/" + script;

  CHECK(utils::os::exists(path))
    << "Failed to find webui script at '" << path << "'";

  // TODO(benh): Consider actually forking a process for the webui
  // rather than just creating a thread. This will let us more easily
  // run multiple webui's simultaneously (e.g., the master and
  // slave). This might also give us better isolation from Python
  // interpreter errors (and maybe even remove the need for two C-c to
  // exit the process).

  if (!thread::start(std::tr1::bind(&run, directory, script, args), true)) {
    LOG(FATAL) << "Failed to start webui thread";
  }
}

} // namespace webui {
} // namespace utils {
} // namespace internal {
} // namespace mesos {

#endif // MESOS_WEBUI
