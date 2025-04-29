#include "join.h"

#include <iostream>

#include <print>

void join(
    PipelineQueue<QueueItem, Metadata> & inputQueue,
    const char * filename,
    ProgressCallback * progressCallback, 
    ErrorCallback * errorCallback
){
  auto optOutputFormatContext = OutputFormatContext::open(filename);
  if(!optOutputFormatContext)
    return errorCallback("Failed to open input format context");
  auto & outputFormatContext = optOutputFormatContext.value();

  auto 
    * videoSteam = avformat_new_stream(outputFormatContext, nullptr),
    * audioStream = avformat_new_stream(outputFormatContext, nullptr);

  if(!videoSteam || !audioStream) 
    return errorCallback("Error allocating output streams");

  Metadata metadata;
  inputQueue.getMetadata(metadata);

  if(avcodec_parameters_copy(videoSteam->codecpar, metadata.videoStream->codecpar) < 0) 
    return errorCallback("Error copying video codec parameters");
  if(avcodec_parameters_copy(audioStream->codecpar, metadata.audioStream->codecpar) < 0) 
    return errorCallback("Error copying audio codec parameters");

  videoSteam->codecpar->codec_tag = 0;
  audioStream->codecpar->codec_tag = 0;

  videoSteam->time_base = metadata.videoStream->time_base;
  audioStream->time_base = metadata.audioStream->time_base;

  videoSteam->index = metadata.videoStream->index;
  audioStream->index = metadata.audioStream->index;

  avformat_write_header(outputFormatContext, nullptr);

  // Loop through each input context and write its packets to the output file
  for(QueueItem chunk; inputQueue.pop(chunk);) 
  {
    for(auto & packet : chunk.packets) 
      av_write_frame(outputFormatContext, packet);
  } 
}