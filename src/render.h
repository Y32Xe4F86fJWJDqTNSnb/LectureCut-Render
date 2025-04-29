#pragma once

#if defined(_MSC_VER)
  //  Microsoft 
  #define EXPORT __declspec(dllexport)
#elif defined(__GNUC__)
  //  GCC
  #define EXPORT __attribute__((visibility("default")))
#else
  #define EXPORT
  #pragma warning Unknown dynamic link export semantics.
#endif

#include <stdint.h>

#ifdef __cplusplus
extern "C" 
{
#endif

  typedef void ProgressCallback(const char*, double);
  typedef void ErrorCallback(const char *);
  
  EXPORT const char * version(ErrorCallback * errorCallback);

  EXPORT void init(ErrorCallback * errorCallback);

  struct Cut
  {
    int64_t start;
    int64_t end;
  };

  struct CutList
  {
    long num_cuts;
    Cut * cuts;
  };

  struct ArgumentResult 
  {
    const char * name;
    const char * value;
  };

  struct ArgumentResultList 
  {
    long num_args;
    ArgumentResult * args;
  };

  EXPORT void render(
    const char * file,
    const char * output,
    CutList cuts,
    ArgumentResultList args,
    ProgressCallback * progressCallback,
    ErrorCallback * errorCallback
  );

  struct Argument 
  {
    const char short_name;
    const char * long_name;
    const char * description;
    bool required;
    bool is_flag;
  };

  struct ArgumentList 
  {
    long num_args;
    Argument * args;
  };

  EXPORT ArgumentList get_arguments(ErrorCallback * errorCallback);

#ifdef __cplusplus
}
#endif