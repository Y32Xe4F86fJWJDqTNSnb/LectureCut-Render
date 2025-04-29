#pragma once

#include "pipeline.h"
#include "common.h"
#include "../definitions.h"
#include "../render.h"

#include "../libav.h"

void transcode_audio(
  PipelineQueue<QueueItem, Metadata> & inputQueue,
  PipelineQueue<QueueItem, Metadata> & outputQueue,
  int quality,
  CutList const & cutList,
  ProgressCallback * progressCallback, 
  ErrorCallback * errorCallback
);

static void remove_disposable(std::vector<AVPacket *> & packets);

static void handle_segment_contained_in_one_cut(
  std::vector<Packet> & packets_in,
  AVRational const & native_stream_timebase,
  std::vector<LocalCut> const & segment_cuts, 
  int64_t centiseconds_of_complete_cuts_kept_before_segment, 
  int64_t segment_start, 
  int64_t segment_end, 
  int64_t & time_audio_delay_prev
);

static void handle_segment_with_multiple_cuts(
  std::vector<Packet> & packets_in,
  AVRational const & native_stream_timebase,
  std::vector<LocalCut> const & segment_cuts, 
  int64_t centiseconds_of_complete_cuts_kept_before_segment, 
  int64_t segment_start, 
  int64_t segment_end, 
  int64_t & time_audio_delay_prev
);