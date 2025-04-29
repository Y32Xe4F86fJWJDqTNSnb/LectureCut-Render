#pragma once

#include "pipeline.h"
#include "common.h"
#include "../definitions.h"
#include "../render.h"

#include "../libav.h"

void join(
    PipelineQueue<QueueItem, Metadata> & inputQueue,
    const char * filename,
    ProgressCallback * progressCallback, 
    ErrorCallback * errorCallback
);