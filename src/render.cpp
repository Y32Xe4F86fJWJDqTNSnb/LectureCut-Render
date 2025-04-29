#include "render.h"
#include "definitions.h"
#include "pipeline/pipeline.h"

#include "pipeline/segmentation.h"
#include "pipeline/video.h"
#include "pipeline/audio.h"
#include "pipeline/join.h"

#include "arguments.hpp"
#include "exceptionCatcher.hpp"

#include "libav.h"

#include <numeric>
#include <ranges>
#include <thread>

char const * version(ErrorCallback *)
{
  return VERSION_NAME;
}

void init(ErrorCallback *)
{
  #if PRINT_VERBOSE
    av_log_set_level(AV_LOG_VERBOSE);
  #else
    av_log_set_level(AV_LOG_ERROR);
  #endif
  
  avformat_network_init();
}

ArgumentList get_arguments(ErrorCallback *)
{
  constexpr static int NUM_ARGS = 1;
  auto * args = 
    new Argument[NUM_ARGS]
    {
      { 'q', "quality", "The quality of the output video", false, false }
    };

  return {NUM_ARGS, args};
}

void render(
    const char * file,
    const char * output,
    CutList cuts,
    ArgumentResultList args,
    ProgressCallback * progressCallback,
    ErrorCallback * errorCallback
){
  int quality = 23;

  updateParamsFromArgs(
    args, errorCallback, 
    std::forward_as_tuple("quality", quality)
  );

  PipelineQueue<QueueItem, Metadata> 
    segment_video_queue,
    segment_audio_queue,
    join_queue;
  
  std::vector<ExceptionCaughtThread> workers;
  auto const numThreadsPerQueue = 1u; //std::max(1u, (std::thread::hardware_concurrency() - 2) / 2);

  ExceptionCaughtThread 
    join_thread(errorCallback, join, join_queue, output, progressCallback, worker_thread_error_callback);

  for(auto threadIdx : std::views::iota(0u, numThreadsPerQueue))
    workers.emplace_back(errorCallback, transcode_audio, segment_audio_queue, join_queue, quality, cuts, progressCallback, worker_thread_error_callback);
  
  for(auto threadIdx : std::views::iota(0u, numThreadsPerQueue))
    workers.emplace_back(errorCallback, transcode_video, segment_video_queue, join_queue, quality, cuts, progressCallback, worker_thread_error_callback);

  ExceptionCaughtThread 
    segment_thread(errorCallback, segment, file, segment_video_queue, segment_audio_queue, progressCallback, worker_thread_error_callback);
}