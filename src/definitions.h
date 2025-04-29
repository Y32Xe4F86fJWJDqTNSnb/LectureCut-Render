#pragma once

#include <stdexcept>

constexpr static char 
  VERSION_NAME[] = {"0.2.0"},
  PROGRESS_BAR_NAME[] = {"Cut Down"};

#define DEFAULT_FFMPEG_LOG_LEVEL "ERROR"

#define PRINT_VERBOSE 0

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

#ifdef NDEBUG
#  define assert(condition) ((void)0)
#else
#  define assert(condition) /*implementation defined*/
#endif

static void worker_thread_error_callback(char const * msg)
{
  throw std::runtime_error(msg);
}