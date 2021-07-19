#define AUDIO_CAPTURE_FORMAT SND_PCM_FORMAT_FLOAT_LE
#define DSP_MODE_NONE 0
#define DSP_MODE_THRESHOLD 1
#define DSP_MODE_LOW_PASS 2

//counts number of thresholds reached
void process_threshold(thread_context * context, float  * buffer) {
    unsigned int i;
    unsigned int threshold_reached = 0;
    unsigned int num_samples = context->audio_capture.dsp_buffer_sample_count * context->audio_capture.channel_count;
    float threshold = context->audio_capture.threshold;

    for (i = 0; i < context->audio_capture.dsp_buffer_sample_count * context->audio_capture.channel_count;i++) {
        if (abs(buffer[i]) >= threshold) {
            threshold_reached++;
        }
    }
    
    pthread_mutex_lock(&context->audio_capture.buffer_mutex);
    *((unsigned int*)context->audio_capture.capture_dst_buffer) += threshold_reached;
    context->audio_capture.capture_dst_buffer_count += context->audio_capture.dsp_buffer_sample_count;
    pthread_mutex_unlock(&context->audio_capture.buffer_mutex);
}

void process_low_pass(thread_context* context, float* buffer) {
    unsigned int i;
    float res = 0.0;
    for (i = 0; i < context->audio_capture.dsp_buffer_sample_count; i++) {
        res += abs(buffer[i]);
    }
    res = res / (float)context->audio_capture.dsp_buffer_sample_count;
    pthread_mutex_lock(&context->audio_capture.buffer_mutex);
    *((float*)context->audio_capture.capture_dst_buffer) = res;
    pthread_mutex_unlock(&context->audio_capture.buffer_mutex);
}


void capture_thread_func(thread_context* context) {
    char* buffer;
    int err;
    int i=0;
    buffer = malloc(context->audio_capture.dsp_buffer_sample_count * snd_pcm_format_width(AUDIO_CAPTURE_FORMAT) / 8 * context->audio_capture.channel_count);

    while (context->audio_capture.capturing) {
        if ((err = snd_pcm_readi(context->audio_capture.handle, buffer, context->audio_capture.dsp_buffer_sample_count)) != context->audio_capture.dsp_buffer_sample_count) {

        } else {
            switch (context->audio_capture.dsp_mode) {
            case DSP_MODE_NONE:
                if (debug && ((i % context->audio_capture.rate)==0)) printf("Processed %d audio samples.\n", i);
                i += context->audio_capture.dsp_buffer_sample_count;
                break;
            case DSP_MODE_THRESHOLD: //
                process_threshold(context, (float*)buffer);
                break;
            case DSP_MODE_LOW_PASS:
                process_low_pass(context, (float*)buffer);
                break;
            }
        }
    }

    free(buffer);

    snd_pcm_hw_params_free(context->audio_capture.hw_params);

    snd_pcm_close(context->audio_capture.handle);
    if (debug) fprintf(stdout, "Audio interface closed.\n");
}

void stop_record(thread_context* context, char* args) {

    context->audio_capture.capturing = 0;
    int res = pthread_join(context->audio_capture.thread, NULL); //wait for thread to finish, clean up and exit
    if (res != 0) {
        fprintf(stderr, "Error join thread in stop_record: %d, %d ", context->id, res);
        perror(NULL);
    }

}


//record_audio <device>,<sample_rate>,<sample_count>,<channels>
void record_audio(thread_context* context, char* args) {
    char device_name[MAX_VAL_LEN] = { 0 };
    int err;
    int channels=2; //record stereo
    unsigned int wanted_rate = 24000;
    unsigned int rate;
    unsigned int sample_count = 1024;

    ///snd_pcm_hw_params_t* hw_params;
    snd_pcm_format_t format = AUDIO_CAPTURE_FORMAT; //

    args = read_str(args, device_name, MAX_VAL_LEN);
    args = read_int(args, &wanted_rate);
    args = read_int(args, &channels);
    args = read_int(args, &sample_count);

    rate = wanted_rate;
    if ((err = snd_pcm_open(&context->audio_capture.handle, device_name, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        fprintf(stderr, "Cannot open audio device %s (%s).\n", device_name, snd_strerror(err));
        return;
    }

    if (debug) fprintf(stdout, "Audio interface %s opened.\n", device_name);

    if ((err = snd_pcm_hw_params_malloc(&context->audio_capture.hw_params)) < 0) {
        fprintf(stderr, "Cannot allocate hardware parameter structure (%s).\n", snd_strerror(err));
        snd_pcm_close(context->audio_capture.handle);
        return;
    }

    if (debug) fprintf(stdout, "hw_params allocated\n");

    if ((err = snd_pcm_hw_params_any(context->audio_capture.handle, context->audio_capture.hw_params)) < 0) {
        fprintf(stderr, "Cannot initialize hardware parameter structure (%s).\n", snd_strerror(err));
        snd_pcm_hw_params_free(context->audio_capture.hw_params);
        snd_pcm_close(context->audio_capture.handle);
        return;
    }

    if (debug) fprintf(stdout, "hw_params initialized\n");

    if ((err = snd_pcm_hw_params_set_access(context->audio_capture.handle, context->audio_capture.hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        fprintf(stderr, "Cannot set access type (%s).\n", snd_strerror(err));
        snd_pcm_hw_params_free(context->audio_capture.hw_params);
        snd_pcm_close(context->audio_capture.handle);
        return;
    }

    if (debug) fprintf(stdout, "hw_params access setted\n");

    if ((err = snd_pcm_hw_params_set_format(context->audio_capture.handle, context->audio_capture.hw_params, format)) < 0) {
        fprintf(stderr, "Cannot set sample format (%s).\n", snd_strerror(err));
        snd_pcm_hw_params_free(context->audio_capture.hw_params);
        snd_pcm_close(context->audio_capture.handle);
        return;
    }

    if (debug) fprintf(stdout, "hw_params format setted\n");

    if ((err = snd_pcm_hw_params_set_rate_near(context->audio_capture.handle, context->audio_capture.hw_params, &rate, 0)) < 0) {
        fprintf(stderr, "Cannot set sample rate (%s).\n", snd_strerror(err));
        snd_pcm_hw_params_free(context->audio_capture.hw_params);
        snd_pcm_close(context->audio_capture.handle);
        return;
    }

    if (rate != wanted_rate) {
        fprintf(stderr, "Warning wanted sample rate of %d not available, using %d.\n", wanted_rate, rate);
    }

    if (debug) fprintf(stdout, "hw_params rate setted\n");

    if ((err = snd_pcm_hw_params_set_channels(context->audio_capture.handle, context->audio_capture.hw_params, channels)) < 0) {
        fprintf(stderr, "cannot set channel count (%s)\n", snd_strerror(err));
        snd_pcm_hw_params_free(context->audio_capture.hw_params);
        snd_pcm_close(context->audio_capture.handle);
        return;
    }

    if (debug) fprintf(stdout, "hw_params channels setted\n");

    if ((err = snd_pcm_hw_params(context->audio_capture.handle, context->audio_capture.hw_params)) < 0) {
        fprintf(stderr, "Cannot set parameters (%s).\n", snd_strerror(err));
        snd_pcm_hw_params_free(context->audio_capture.hw_params);
        snd_pcm_close(context->audio_capture.handle);
        return;
    }

    if ((err = snd_pcm_prepare(context->audio_capture.handle)) < 0) {
        fprintf(stderr, "Cannot prepare audio interface for use (%s).\n", snd_strerror(err));
        snd_pcm_hw_params_free(context->audio_capture.hw_params);
        snd_pcm_close(context->audio_capture.handle);
        return;
    }

    if (debug) fprintf(stdout, "Audio interface prepared.\n");

    context->audio_capture.capturing = 1;
    context->audio_capture.channel_count = channels;
    context->audio_capture.dsp_mode = DSP_MODE_NONE;
    context->audio_capture.dsp_buffer_sample_count = sample_count;
    context->audio_capture.rate = rate;
    context->audio_capture.format = format;
    context->audio_capture.capture_dst_buffer = NULL;
    int s = pthread_create(&context->audio_capture.thread, NULL, (void* (*)(void*)) & capture_thread_func, context);
    if (s != 0) {
        fprintf(stderr, "Error creating new capture thread: %d.", s);
        snd_pcm_hw_params_free(context->audio_capture.hw_params);
        snd_pcm_close(context->audio_capture.handle);
        context->audio_capture.capturing = 0;
        perror(NULL);
    }

    if (debug) fprintf(stdout, "Capturing OK.\n");

}