#include "segmentation.h"
#include "../definitions.h"

#include <print>

#include <algorithm>

void segment(
  const char * filename,
  PipelineQueue<QueueItem, Metadata> & videoOutputQueue,
  PipelineQueue<QueueItem, Metadata> & audioOutputQueue,
  ProgressCallback * progressCallback, 
  ErrorCallback * errorCallback
)
{
  std::shared_ptr<InputFormatContext> spInputFormatContext;
  {
    auto optInputFormatContext = InputFormatContext::open(filename);
    if(!optInputFormatContext)
      return errorCallback("Failed to open input format context");
    spInputFormatContext = std::make_shared<InputFormatContext>(std::move(optInputFormatContext.value()));
  }
  auto & inputFormatContext = *spInputFormatContext.get();

  std::span streams {inputFormatContext->streams, inputFormatContext->nb_streams};

  auto const firstStreamIdxOfType = 
    [](std::span<AVStream *> const & streams, AVMediaType type) -> std::ptrdiff_t
    {
      if(
        auto const itFind = std::find_if(streams.cbegin(), streams.cend(), [type](AVStream const * stream) { return stream->codecpar->codec_type == type; });
        itFind != streams.cend()
      )
        return (*itFind)->index;
      else
        return -1;
    };

  auto 
    videoStreamIndex = firstStreamIdxOfType(streams, AVMEDIA_TYPE_VIDEO),
    audioStreamIndex = firstStreamIdxOfType(streams, AVMEDIA_TYPE_AUDIO);

  if(videoStreamIndex == -1 || audioStreamIndex == -1) 
    return errorCallback("Error searching for a video or audio stream");

  if(!streams[videoStreamIndex]->codecpar || !streams[audioStreamIndex]->codecpar) 
    return errorCallback("Error finding video or audio codec parameters");

  Metadata metadata {
    .spInputFormatContext = spInputFormatContext,
    .videoStream = inputFormatContext->streams[videoStreamIndex],
    .audioStream = inputFormatContext->streams[audioStreamIndex]
  };
  
  auto const tb = inputFormatContext->streams[videoStreamIndex]->time_base;
  auto const duration = inputFormatContext->duration / static_cast<double>(AV_TIME_BASE);

  progressCallback(PROGRESS_BAR_NAME, 0.0);

  videoOutputQueue.registerProducerActive();
  audioOutputQueue.registerProducerActive();

  videoOutputQueue.setMetadata(metadata);
  audioOutputQueue.setMetadata(std::move(metadata));

    auto announceProgress =
    [
      progressCallback, 
      mult = static_cast<double>(tb.num) / tb.den / duration, 
      packetIdx = std::size_t(0)
    ] 
    (int64_t pts) mutable
    {
      if(packetIdx % 1000 == 0)
        progressCallback(PROGRESS_BAR_NAME, pts * mult);

      ++packetIdx;
    };

  std::vector<Packet> 
    video_packets,
    audio_packets;
  for(std::optional<Packet> optPacket; optPacket = inputFormatContext.readPacket();) 
  {
    auto & packet = optPacket.value();
    
    if(packet->stream_index == videoStreamIndex) 
    {
      announceProgress(packet.pts());

      // Check if we need to start a new segment
      if(packet.isFlaggedAs(AV_PKT_FLAG_KEY)) 
      {
        // Send the current segment in the pipeline
        videoOutputQueue.push(QueueItem {std::move(video_packets)});
        audioOutputQueue.push(QueueItem {std::move(audio_packets)});
      }

      video_packets.emplace_back(std::move(packet));
    } 
    else if(packet->stream_index == audioStreamIndex) 
    {
      audio_packets.emplace_back(std::move(packet));
    }
  }

  videoOutputQueue.registerProducerDone();
  audioOutputQueue.registerProducerDone();

  progressCallback(PROGRESS_BAR_NAME, 1.0);
}
