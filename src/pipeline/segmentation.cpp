#include "pipeline.h"

extern "C" {
  #include "libavcodec/avcodec.h"
  #include "libavformat/avformat.h"
}

void segment(
  const char *file,
  PIPELINE_QUEUE<QUEUE_ITEM, METADATA*> *video_output_queue,
  PIPELINE_QUEUE<QUEUE_ITEM, METADATA*> *audio_output_queue
)
{
  size_t video_queue_id = video_output_queue->set_working();
  size_t audio_queue_id = audio_output_queue->set_working();

  AVFormatContext *inputFormatContext = nullptr;
  AVPacket pkt;
  int videoStreamIndex = -1;
  int audioStreamIndex = -1;
  std::vector<AVPacket *> *video_packets = nullptr;
  std::vector<AVPacket *> *audio_packets = nullptr;

  if (avformat_open_input(&inputFormatContext, file, NULL, NULL) < 0) {
    throw std::runtime_error("error opening input file");
  }

  if (avformat_find_stream_info(inputFormatContext, NULL) < 0) {
    throw std::runtime_error("error finding stream info");
  }

  for (unsigned int i = 0; i < inputFormatContext->nb_streams; i++) {
    if (inputFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      videoStreamIndex = i;
    } else if (inputFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      audioStreamIndex = i;
    }
  }

  if (videoStreamIndex == -1 || audioStreamIndex == -1) {
    throw std::runtime_error("error finding video or audio stream");
  }

  if (inputFormatContext->streams[videoStreamIndex]->codecpar == nullptr ||
      inputFormatContext->streams[audioStreamIndex]->codecpar == nullptr) {
    throw std::runtime_error("error finding video or audio codec parameters");
  }

  AVCodecParameters *videoCodecParametersCopy = avcodec_parameters_alloc();
  AVCodecParameters *audioCodecParametersCopy = avcodec_parameters_alloc();
  if (avcodec_parameters_copy(videoCodecParametersCopy, inputFormatContext->streams[videoStreamIndex]->codecpar) < 0) {
    throw std::runtime_error("error copying video codec parameters");
  }
  if (avcodec_parameters_copy(audioCodecParametersCopy, inputFormatContext->streams[audioStreamIndex]->codecpar) < 0) {
    throw std::runtime_error("error copying audio codec parameters");
  }

  METADATA *metadata = new METADATA();
  metadata->format_ctx = inputFormatContext;
  metadata->video_stream = inputFormatContext->streams[videoStreamIndex];
  metadata->audio_stream = inputFormatContext->streams[audioStreamIndex];
  audio_output_queue->set_special(&metadata);
  video_output_queue->set_special(&metadata);

  while (av_read_frame(inputFormatContext, &pkt) == 0) {
    if (pkt.stream_index != videoStreamIndex && pkt.stream_index != audioStreamIndex)
    {
      av_packet_unref(&pkt);
      // Skip this packet
      continue;
    }

    AVPacket *packet = av_packet_clone(&pkt);

    // Check if we need to start a new segment
    if (packet->stream_index == videoStreamIndex && packet->flags & AV_PKT_FLAG_KEY) {
      // Send the current segment in the pipeline
      
      if (video_packets) {
        QUEUE_ITEM *item = new QUEUE_ITEM();
        item->packets = video_packets;
        video_output_queue->push(item);
      }
      if (audio_packets) {
        QUEUE_ITEM *item = new QUEUE_ITEM();
        item->packets = audio_packets;
        audio_output_queue->push(item);
      }

      // Start a new segment
      video_packets = new std::vector<AVPacket *>();
      audio_packets = new std::vector<AVPacket *>();
    }

    if (!video_packets || !audio_packets) {
      av_packet_unref(packet);
      throw std::runtime_error("error starting new segment");
    }
    
    if (packet->stream_index == videoStreamIndex) {
      video_packets->push_back(packet);
    } else if (packet->stream_index == audioStreamIndex) {
      audio_packets->push_back(packet);
    }
  }

  video_output_queue->set_done(video_queue_id);
  audio_output_queue->set_done(audio_queue_id);
}
