#include "video.h"

#include <memory>

#include <span>
#include <ranges>

#include <string>
#include <format>

#include <functional>

void transcode_video(
  PipelineQueue<QueueItem, Metadata> & inputQueue,
  PipelineQueue<QueueItem, Metadata> & outputQueue,
  int quality,
  CutList const & cutList,
  ProgressCallback * progressCallback, 
  ErrorCallback * errorCallback
){
  outputQueue.registerProducerActive();

  // relay metadata
  Metadata metadata;
  inputQueue.getMetadata(metadata);

  auto const native_stream_timebase = metadata.videoStream->time_base;

  std::vector<LocalCut> cuts(cutList.num_cuts);
  std::span<Cut> cut_list_span(cutList.cuts, cutList.num_cuts);

  // convert all cuts to time base
  auto const offset = 
    (metadata.videoStream->start_time != AV_NOPTS_VALUE) 
    ? metadata.videoStream->start_time 
    : 0;

  outputQueue.setMetadata(std::move(metadata));

  for(std::size_t i = 0; i < static_cast<std::size_t>(cutList.num_cuts); ++i)
  {
    cuts[i].start = 
      av_rescale_q(cutList.cuts[i].start, CUT_TIMEBASE, native_stream_timebase) + offset;
    cuts[i].end = 
      av_rescale_q(cutList.cuts[i].end, CUT_TIMEBASE, native_stream_timebase) + offset;
  }

  #if PRINT_VERBOSE
  { // DEBUGGING
    std::println(
      syncCout, "Video Timebase: {:d}/{:d}",
      native_stream_timebase.num, native_stream_timebase.den
    );
  }
  #endif

  // loop prep
  QueueItem chunk;

  auto 
    first_cut_in_cur_seg = cuts.begin(),
    cut_to_be_exited = first_cut_in_cur_seg;

  std::vector<LocalCut> segment_cuts {};

  int64_t centiseconds_of_complete_cuts_kept_before_segment = 0;

  int64_t time_video_delay_prev = 0;

  auto dts_prev = std::numeric_limits<int64_t>::min();

  for(std::optional<std::size_t> itemIdx = std::nullopt; itemIdx = inputQueue.pop(chunk);)
  {
    auto interlacedItemIdx = (itemIdx.value() << 1);

    if(chunk.packets.empty())
    {
      outputQueue.push(std::move(chunk), interlacedItemIdx);
      continue;
    }

    auto const [segment_packets_first_by_pts, segment_packets_last_by_pts] = std::ranges::minmax_element(chunk.packets, ptsLess);
    auto const 
      segment_start = segment_packets_first_by_pts->pts(),
      segment_end = segment_packets_last_by_pts->getEndPts();

    // find all cuts in this segment
    segment_cuts.clear();
    {
      auto cut_current = first_cut_in_cur_seg;
      for(
        ;(
          cut_current != cuts.end() 
          && cut_current->start < segment_end
        );
        std::advance(cut_current, 1)
      ){
        segment_cuts.push_back(*cut_current);
        if(cut_current->end > segment_end)
          break;
      }
      first_cut_in_cur_seg = cut_current;
    }
    
    // find the cut that will be completed / exited from next
    while(cut_to_be_exited->end <= segment_start) 
    {
      const auto & last_cut_in_prev_seg = cut_list_span[cut_to_be_exited-cuts.begin()];
      centiseconds_of_complete_cuts_kept_before_segment += last_cut_in_prev_seg.end - last_cut_in_prev_seg.start;
      ++cut_to_be_exited;
    }

    if(segment_cuts.empty()) // this segment isn't part of any cuts
    {
      // the segment will be removed entirely
      chunk.packets.clear();
    }
    else 
    {
      if( // this segment is entirely contained in one cut
        segment_cuts.size() == 1 
        && segment_cuts.front().start <= segment_start 
        && segment_cuts.front().end >= segment_end
      ) // segment will be kept
      {
        handle_segment_contained_in_one_cut(
          chunk.packets,
          native_stream_timebase,
          segment_cuts, 
          centiseconds_of_complete_cuts_kept_before_segment, 
          segment_start, 
          segment_end, 
          time_video_delay_prev,
          dts_prev
        );
      }
      else // the segment is part of multiple cuts
      {
        handle_segment_with_multiple_cuts(
          chunk.packets,
          native_stream_timebase,
          segment_cuts, 
          centiseconds_of_complete_cuts_kept_before_segment, 
          segment_start, 
          segment_end, 
          time_video_delay_prev,
          dts_prev
        );
      }
    }

    outputQueue.push(std::move(chunk), interlacedItemIdx);
  }

  #if PRINT_VERBOSE
  {  // DEBUGGING
    std::println(
      syncCout, "Centiseconds kept in thread #{} up until the start of the last segment: {:d}",
      std::this_thread::get_id(), centiseconds_of_complete_cuts_kept_before_segment
    );
  }
  #endif

  outputQueue.registerProducerDone();
}

void remove_disposable(std::vector<Packet> & packets)
{
  std::erase_if(
    packets,
    [] (auto const & packet) {
      return packet.isFlaggedAs(AV_PKT_FLAG_DISPOSABLE);
    }
  );

  std::ranges::for_each(
    packets,
    [] (auto & packet) {
      if(packet.isFlaggedAs(AV_PKT_FLAG_DISCARD))
          packet.pts() = AV_NOPTS_VALUE;
    }
  );
}

void handle_segment_contained_in_one_cut(
  std::vector<Packet> & packets,
  AVRational const & native_stream_timebase,
  std::vector<LocalCut> const & segment_cuts, 
  int64_t centiseconds_of_complete_cuts_kept_before_segment, 
  int64_t segment_start, 
  int64_t segment_end, 
  int64_t & time_video_delay_prev,
  int64_t & dts_prev
){
  auto const time_discarded_before_first_and_only_cut_in_segment = 
    segment_cuts.front().start 
    - av_rescale_q(centiseconds_of_complete_cuts_kept_before_segment, CUT_TIMEBASE, native_stream_timebase);

  auto time_video_delay = time_video_delay_prev;
  // determine how much later the end of the cut occurs compared to the end of the last packet falling in it
  if(segment_cuts.front().end <= segment_end) 
  {
    auto const & r_last_packet_in_cut = packets.back();
    auto const end_pts_of_last_packet_in_cut = r_last_packet_in_cut.getEndPts();
    time_video_delay_prev = segment_cuts.front().end - end_pts_of_last_packet_in_cut;
  }
  // determine how much sooner the start of the first packet falling into a cut occurs compared to the start of that cut
  if(segment_cuts.front().start >= segment_start) 
  {
    auto const & first_packet_in_cut = packets.front();
    time_video_delay += first_packet_in_cut.pts() - segment_cuts.front().start;
  }

  for(auto & packet : packets)
  {
    auto const offset = time_discarded_before_first_and_only_cut_in_segment + time_video_delay;

    packet.dts() -= offset;

    // just checking in case the input video is faulty
    if(packet.pts() != AV_NOPTS_VALUE)
      packet.pts() -= offset;
    else
      packet.addFlags(AV_PKT_FLAG_DISCARD);
  }

  // start marking packets going backwards from the end of the cut as to not be displayed until the delay is accounted for
  for(
    auto it_packet = packets.rbegin(); 
    (
      it_packet != packets.rend() 
      && std::abs(time_video_delay) >= std::abs(time_video_delay - (*it_packet).duration())
    ); 
    std::advance(it_packet, 1)
  ){
    Packet & r_packet = *it_packet;

    time_video_delay -= r_packet.duration();
    r_packet.addFlags(AV_PKT_FLAG_DISCARD);
  }

  // if dts_prev has not been set yet, set it to the earliest dts in the segment
  if(dts_prev == std::numeric_limits<int64_t>::min())
    dts_prev = packets.front().dts();

  for(auto & packet : packets) 
  {
    // if the dts hasn't increased
    if(packet.dts() <= dts_prev) 
    {
      packet.dts() = dts_prev + 1;
      // if the above line leads 'pts >= dts' to be violated
      if(packet.pts() < packet.dts()) 
        packet.pts() = packet.dts();
    }
    dts_prev = packet.dts();
  }

  #if PRINT_VERBOSE
  { // DEBUGGING

      static constexpr auto decimal_digits_in_int64_t = std::to_string(std::numeric_limits<int64_t>::max()).length();

      std::string message;

      std::format_to(
        std::back_inserter(message),  
        "\n{1:>14} | Video | Keeping as is"
        "\n{2:>14} | Time discarded by now: {3:.>{0}d} | Drift before : {4:.>{0}d} | Start: {5:.>{0}d} | End: {6:.>{0}d}\n",
        decimal_digits_in_int64_t,
        "Segment Info",
        "Cut Info", 
        time_discarded_before_first_and_only_cut_in_segment,
        time_video_delay,
        segment_cuts.cbegin()->start,
        segment_cuts.cbegin()->end
      );
      //<< " | Idx: " << std::distance(cuts.begin(), cut_to_be_exited) << " (" << cut_idx << " in segment)" 

      auto packets_sorted_pts = packets;
      std::ranges::stable_sort(packets_sorted_pts, ptsLess);
      for(auto const * packet_sorted_pts_after_cut : packets_sorted_pts)
      {
        std::format_to(
          std::back_inserter(message), 
          "\n{1:>14} | PTS in s: {2:10.5f} | PTS: {3:.>{0}d} | DTS: {4:.>{0}d} | PTS geq DTS: {5:d}",
          decimal_digits_in_int64_t,
          "Packet Info",
          av_rescale_q(packet_sorted_pts_after_cut.pts(), native_stream_timebase, TIMEBASE_1MS)/1000.0,
          packet_sorted_pts_after_cut.pts(),
          packet_sorted_pts_after_cut.dts(),
          packet_sorted_pts_after_cut.pts() >= packet_sorted_pts_after_cut.dts()
        );
      }

      std::println(syncCout, "{:s}", std::move(message));
  }
  #endif
}

void handle_segment_with_multiple_cuts(
  std::vector<Packet> & packets,
  AVRational const & native_stream_timebase,
  std::vector<LocalCut> const & segment_cuts, 
  int64_t centiseconds_of_complete_cuts_kept_before_segment, 
  int64_t segment_start, 
  int64_t segment_end, 
  int64_t & time_video_delay_prev,
  int64_t & dts_prev
){
  auto const time_of_complete_cuts_kept_before_segment = av_rescale_q(centiseconds_of_complete_cuts_kept_before_segment, CUT_TIMEBASE, native_stream_timebase);

  // copy pointers to input packets
  auto const up_packets_sorted_pts = std::make_unique_for_overwrite<Packet * []>(packets.size());
  std::span packets_sorted_pts {up_packets_sorted_pts.get(), packets.size()};
  std::ranges::transform(packets, packets_sorted_pts.begin(), projRefToPtr);
  // sort pointers to input packets by their presentation timestamps
  std::ranges::stable_sort(packets_sorted_pts, ptsLess, projPtrToRef);

  auto const packets_sorted_pts_chunked_by_cut_starts_size = segment_cuts.size()+1;
  auto const packets_sorted_pts_chunked_by_cut_starts = std::make_unique<std::vector<Packet *>[]>(packets_sorted_pts_chunked_by_cut_starts_size);
  for(
    auto cut_current = segment_cuts.cbegin(); 
    auto * packet : packets_sorted_pts
  ){
    auto & r_packet = *packet;

    bool hide_packet = true;

    // advance cut_current until we find the cut before whose end the current packet lies in terms of pts
    while(cut_current->end < r_packet.pts() && cut_current < segment_cuts.cend())
      std::advance(cut_current, 1);

    // if we are past the last cut in the segment
    if(cut_current == segment_cuts.cend()) 
    {
      // mark the packet as to be removed
      r_packet.addFlags(AV_PKT_FLAG_DISPOSABLE);
    }
    // if the end of the presentation of the packet is after the start of the cut
    else if(
      auto const packet_presentation_end = r_packet.getEndPts();
      packet_presentation_end > cut_current->start
    ){
      // if the end of the presentation of the packet is before or on the end of the cut
      if(packet_presentation_end <= cut_current->end) 
      {
        // do not hide it, it needs to be displayed
        hide_packet = false;
      }
      // else keep the packet for decoding but do not display it
    }
    // else keep the packet for decoding but do not display it

    if(hide_packet) 
    {
      // use packet for decoding but do not display it
      r_packet.addFlags(AV_PKT_FLAG_DISCARD);
    }

    // save the packet as part of this cut
    packets_sorted_pts_chunked_by_cut_starts[std::distance(segment_cuts.cbegin(), cut_current)].push_back(packet);
  }

  constexpr auto packet_displayed = 
    [] (Packet const & packet) {
        return
          // the packet is going to be displayed, because
          packet.isNotFlaggedAs(AV_PKT_FLAG_DISPOSABLE, AV_PKT_FLAG_DISCARD) // it is neither going to be deleted, nor going to be hidden 
          && packet.pts() != AV_NOPTS_VALUE; // and has a presentation timestamp
    };

  auto const 
    // array to carry the time discarded up until each cut in the segment
    time_discarded_before_cuts = std::make_unique_for_overwrite<int64_t[]>(segment_cuts.size()),
    // array to carry the video delay up until each cut in the segment
    time_video_delay_before_cuts = std::make_unique_for_overwrite<int64_t[]>(segment_cuts.size());
  // initialize variable to store the sum of time from all cuts passed in the segment
  int64_t time_of_complete_cuts_kept_within_segment = 0;
  for(
    std::size_t cut_idx = 0; 
    auto const & cut_current : segment_cuts
  ){
    time_discarded_before_cuts[cut_idx] = cut_current.start - (time_of_complete_cuts_kept_before_segment + time_of_complete_cuts_kept_within_segment);
    time_of_complete_cuts_kept_within_segment += cut_current.end - cut_current.start;

    time_video_delay_before_cuts[cut_idx] = time_video_delay_prev;
    // determine how much later the end of the cut occurs compared to the end of the last packet falling in it
    // requires searching for the first displayable packet from the end of the cut first
    if(cut_current.end <= segment_end) 
    {
      auto const it = std::ranges::find_if(
        packets_sorted_pts_chunked_by_cut_starts[cut_idx] | std::views::reverse | std::views::as_const,
        packet_displayed, projPtrToRef
      );

      if(it != packets_sorted_pts_chunked_by_cut_starts[cut_idx].crend()) // if a displayable packet was found 
      {
        auto const & r_last_packet_in_cut = **it;
        auto end_pts_of_last_packet_in_cut = r_last_packet_in_cut.getEndPts();
        time_video_delay_prev = cut_current.end - end_pts_of_last_packet_in_cut;
      } 
    }
    // determine how much sooner the start of the first packet falling into a cut occurs compared to the start of that cut
    // requires searching for the first displayable packet from the start of the cut first
    if(cut_current.start >= segment_start) 
    {
      auto const it = std::ranges::find_if(
        packets_sorted_pts_chunked_by_cut_starts[cut_idx],
        packet_displayed, projPtrToRef
      );

      if(it != packets_sorted_pts_chunked_by_cut_starts[cut_idx].cend()) // if a displayable packet was found
      {
        auto const & r_first_packet_in_cut = **it;
        time_video_delay_before_cuts[cut_idx] += r_first_packet_in_cut.pts() - cut_current.start;
      }
    }

    ++cut_idx;
  }

  // shift all timestamps by the offset appropriate for the cut they're in
  // packets that will be deleted are shifted by the offset of the last cut
  for(std::size_t cut_idx = 0; cut_idx < packets_sorted_pts_chunked_by_cut_starts_size; ++cut_idx)
  {
    auto const cut_idx_clamped = std::min(segment_cuts.size() - 1, cut_idx);

    auto const time_discarded_before_cut = time_discarded_before_cuts[cut_idx_clamped];
    auto time_video_delay = time_video_delay_before_cuts[cut_idx_clamped];
    for(auto * packet_sorted_pts_after_cut : packets_sorted_pts_chunked_by_cut_starts[cut_idx])
    {
      auto const offset = time_discarded_before_cut + time_video_delay;

      packet_sorted_pts_after_cut->dts() -= offset;

      // just checking in case the input video is faulty
      if(packet_sorted_pts_after_cut->pts() != AV_NOPTS_VALUE) 
        packet_sorted_pts_after_cut->pts() -= offset;
      else
        packet_sorted_pts_after_cut->addFlags(AV_PKT_FLAG_DISCARD);
    }

    // start marking packets going backwards from the end the end of the cut as to not be displayed until the delay is accounted for
    for(
      auto it_packet = packets_sorted_pts_chunked_by_cut_starts[cut_idx].rbegin(); 
      (
        it_packet != packets_sorted_pts_chunked_by_cut_starts[cut_idx].rend() 
        && std::abs(time_video_delay - (*it_packet)->duration()) < std::abs(time_video_delay)
      ); 
      std::advance(it_packet, 1)
    ){
      auto & r_packet = **it_packet;	

      // if the packet is one that will be displayed
      if(packet_displayed(r_packet)) 
      {
        time_video_delay -= r_packet.duration();
        r_packet.addFlags(AV_PKT_FLAG_DISCARD);
      }
    }
  }

  // if dts_prev has not been set yet, set it to the earliest dts in the segment
  if(dts_prev == std::numeric_limits<int64_t>::min())
    dts_prev = packets.front().dts();

  for(auto & packet : packets) 
  {
    if(!packet.isFlaggedAs(AV_PKT_FLAG_DISPOSABLE)) 
    {
      // if the packet will be discarded
      if(packet.isFlaggedAs(AV_PKT_FLAG_DISCARD)) 
      {
        // tightly pack it after the last one
        packet.dts() = dts_prev + 1;
        packet.pts() = packet.dts();
      }
      // if the packet won't be discarded and the dts hasn't increased
      else if(packet.dts() <= dts_prev) 
      {
        packet.dts() = dts_prev + 1;
        // if the above line leads 'pts >= dts' to be violated
        if(packet.pts() < packet.dts()) 
          packet.pts() = packet.dts();
      }
      dts_prev = packet.dts();
    }
  }

  #if PRINT_VERBOSE
  { // DEBUGGING
    for(std::size_t cut_idx = 0; cut_idx < packets_sorted_pts_chunked_by_cut_starts_size-1; ++cut_idx)
    {
      static constexpr auto decimal_digits_in_int64_t = std::to_string(std::numeric_limits<int64_t>::max()).length();

      std::string message;

      std::format_to(
        std::back_inserter(message),
        "\n{1:>14} | Video | Cutting up"
        "\n{2:>14} | Time discarded by now: {3:.>{0}d} | Drift before : {4:.>{0}d} | Start: {5:.>{0}d} | End: {6:.>{0}d}\n",
        decimal_digits_in_int64_t,
        "Segment Info",
        "Cut Info", 
        time_discarded_before_cuts[cut_idx],
        time_video_delay_before_cuts[cut_idx],
        std::next(segment_cuts.cbegin(), cut_idx)->start,
        std::next(segment_cuts.cbegin(), cut_idx)->end
      );
      //<< " | Idx: " << std::distance(cuts.begin(), cut_to_be_exited) << " (" << cut_idx << " in segment)" 

      for(auto const * packet_sorted_pts_after_cut : packets_sorted_pts_chunked_by_cut_starts[cut_idx])
      {
        std::format_to(
          std::back_inserter(message), 
          "\n{1:>14} | PTS in s: {2:10.5f} | PTS: {3:.>{0}d} | DTS: {4:.>{0}d} | PTS geq DTS: {5:d} | Hide: {6:d} | Drop: {7:d}",
          decimal_digits_in_int64_t,
          "Packet Info",
          av_rescale_q(packet_sorted_pts_after_cut.pts(), native_stream_timebase, TIMEBASE_1MS)/1000.0,
          packet_sorted_pts_after_cut.pts(),
          packet_sorted_pts_after_cut.dts(),
          packet_sorted_pts_after_cut.pts() >= packet_sorted_pts_after_cut.dts(),
          packet_sorted_pts_after_cut.isFlaggedAs(AV_PKT_FLAG_DISCARD),
          packet_sorted_pts_after_cut.isFlaggedAs(AV_PKT_FLAG_DISPOSABLE)
        );
      }

      std::println(syncCout, "{:s}", std::move(message));
    }
  }
  #endif

  remove_disposable(packets);
}