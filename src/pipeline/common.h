#pragma once

#include "../libav.h"

#include <iostream>
#include <syncstream>

static std::osyncstream syncCout(std::cout);

static AVRational const
  CUT_TIMEBASE = av_make_q(1, 100),
  TIMEBASE_1MS = av_make_q(1, 1000);

constexpr static auto ptsLess = 
  [](Packet const & a, Packet const & b) -> bool
  { 
    return a.pts() < b.pts(); 
  };

constexpr static auto testFlag = 
  [] <std::integral I> (I flags, I flag) -> bool
  { 
    return (flags & flag) == flag; 
  };

constexpr static auto projRefToPtr = [] (auto & t) { return &t; };
constexpr static auto projPtrToRef = [] (auto * t) -> std::remove_pointer_t<decltype(t)> & { return *t; };

struct LocalCut
{
  int64_t start;
  int64_t end;
};

struct QueueItem 
{
  std::vector<Packet> packets;
};

struct Metadata 
{
  std::shared_ptr<InputFormatContext> spInputFormatContext;	
  AVStream 
    * videoStream = nullptr,
    * audioStream = nullptr;	
};