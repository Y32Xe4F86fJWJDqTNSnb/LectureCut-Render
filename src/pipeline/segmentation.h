#pragma once

#include "pipeline.h"
#include "common.h"
#include "../definitions.h"
#include "../render.h"

#include "../libav.h"

constexpr static auto FIELD_UNCHANGED = std::numeric_limits<unsigned int>::max();

void segment(
  const char * filename,
  PipelineQueue<QueueItem, Metadata> & video_output_queue,
  PipelineQueue<QueueItem, Metadata> & audio_output_queue,
  ProgressCallback * progressCallback, 
  ErrorCallback * errorCallback
);