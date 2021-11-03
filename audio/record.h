#ifndef AUDIO_RECORD_H
#define AUDIO_RECORD_H

#include "../ws2812svr.h"

#define AUDIO_CAPTURE_FORMAT SND_PCM_FORMAT_FLOAT_LE
#define DSP_MODE_NONE 0
#define DSP_MODE_THRESHOLD 1
#define DSP_MODE_LOW_PASS 2

void record_audio(thread_context* context, char* args);

#endif