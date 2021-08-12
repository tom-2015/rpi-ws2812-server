#define AUDIO_CAPTURE_FORMAT SND_PCM_FORMAT_FLOAT_LE
#define DSP_MODE_NONE 0
#define DSP_MODE_THRESHOLD 1
#define DSP_MODE_LOW_PASS 2

//counts number of thresholds reached
void process_threshold(thread_context * context, float* buffer, unsigned int len) {
    unsigned int i;
    unsigned int threshold_reached = 0;
    unsigned int num_samples = context->audio_capture.dsp_buffer_sample_count * context->audio_capture.channel_count;
    float threshold = context->audio_capture.threshold;

    for (i = 0; i < len;i++) { //context->audio_capture.dsp_buffer_sample_count * context->audio_capture.channel_count
        if (fabs(buffer[i]) >= threshold) {
            threshold_reached++;
        }
    }
    
    pthread_mutex_lock(&context->audio_capture.buffer_mutex);
    *((unsigned int*)context->audio_capture.capture_dst_buffer) += threshold_reached;
    context->audio_capture.capture_dst_buffer_count += context->audio_capture.dsp_buffer_sample_count;
    pthread_mutex_unlock(&context->audio_capture.buffer_mutex);
}

void process_low_pass(thread_context* context, float* buffer, unsigned int len) {
    unsigned int i;
    float res = 0.0;
    for (i = 0; i < len; i++) {
        res += fabs(buffer[i]);
    }
    res = res / len; //(float)context->audio_capture.dsp_buffer_sample_count;
    pthread_mutex_lock(&context->audio_capture.buffer_mutex);
    *((float*)context->audio_capture.capture_dst_buffer) = res;
    pthread_mutex_unlock(&context->audio_capture.buffer_mutex);
}

void capture_process_frames(thread_context* context, float* buffer, unsigned int len) {
    static int i=0;
    int j;
    float gain = context->audio_capture.capture_gain;
    if (context->audio_capture.capture_gain != 1.0) {
        for (j = 0; j < len; j++) {/*context->audio_capture.dsp_buffer_sample_count*/
            buffer[j] = buffer[j] * gain;
            if (buffer[j] > 1.0) buffer[j] = 1.0;
            else if (buffer[j] < -1.0) buffer[j] = -1.0;
        }
    }
    switch (context->audio_capture.dsp_mode) {
    case DSP_MODE_NONE:
        if (debug && (i >= context->audio_capture.rate)) {
            float avg = 0;
            for (j = 0;j < len ;j++) avg += fabs(buffer[j]); /*context->audio_capture.dsp_buffer_sample_count*/
            avg = avg / (float)i;
            printf("Processed %d audio samples (%f, %d).\n", i, avg, len);
            i = 0;
        } else {
            i += len; /*context->audio_capture.dsp_buffer_sample_count*/
        }
        break;
    case DSP_MODE_THRESHOLD: //
        process_threshold(context, buffer, len);
        break;
    case DSP_MODE_LOW_PASS:
        process_low_pass(context, buffer, len);
        break;
    }
}

void capture_thread_func_pipe(thread_context* context) {
    float* buffer;
    char* named_pipe_file;
    FILE* input_file;
    // pipe://
    named_pipe_file = &context->audio_capture.capture_device_name[7];

    if (strlen(named_pipe_file) == 0) {
        fprintf(stderr, "Unknown named pipe file.\n");
        return;
    }

    while (context->audio_capture.capturing) {
        if (debug) printf("Opening %s as named pipe for audio input.\n", named_pipe_file);
        remove(named_pipe_file);
        mkfifo(named_pipe_file, 0777);
        chmod(named_pipe_file, 0777);
        input_file = fopen(named_pipe_file, "r");

        int num_samples = context->audio_capture.dsp_buffer_sample_count * context->audio_capture.channel_count;
        buffer = (float*)malloc(sizeof(float) * num_samples);

        while (fread((void *) buffer, sizeof(float), num_samples, input_file) == num_samples) {
            capture_process_frames(context, buffer, num_samples);
        }
        
        if (debug) fprintf(stderr, "Error reading named pipe %s.\n", named_pipe_file);
           
    }

    fclose(input_file);
    remove(named_pipe_file);
}

void capture_thread_func_udp(thread_context* context) {
    float* buffer;
    struct sockaddr_in ip_adr;
    int s;
    int port = 6000;
    int broadcast = 0;

    //udpb://
    if (context->audio_capture.capture_device_name[3] == 'b') {
        broadcast = 1;
        port = atoi(&context->audio_capture.capture_device_name[7]);
    } else {
        port = atoi(&context->audio_capture.capture_device_name[6]);
    }

    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (s == -1) {
        fprintf(stderr, "Error opening UDP socket");
        perror("\n");
        return;
    }

    if (broadcast) setsockopt(s, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof broadcast);

    if (debug) printf("Listening on UDP port %d (broadcast=%d).\n", port, broadcast);

    memset(&ip_adr, 0, sizeof(ip_adr));
    ip_adr.sin_family = AF_INET;
    ip_adr.sin_port = htons(port);
    ip_adr.sin_addr.s_addr = INADDR_ANY;

    if (bind(s, (struct sockaddr*)&ip_adr, sizeof(struct sockaddr)) == -1) {
        fprintf(stderr, "Error binding UDP socket");
        perror("\n");
        return;
    }

    buffer = (float*)malloc(0xFFFF);
    while (context->audio_capture.capturing) {
        int received = recvfrom(s, (char*)buffer, 0xFFFF, 0, NULL, NULL);
        capture_process_frames(context, buffer, received / sizeof(float));
    }
    close(s);
    free(buffer);
}

void capture_thread_func_alsa(thread_context* context) {
    float* buffer;
    int err;
    
    buffer = (float*)malloc(context->audio_capture.dsp_buffer_sample_count * snd_pcm_format_width(AUDIO_CAPTURE_FORMAT) / 8 * context->audio_capture.channel_count);

    if (debug) printf("Capturing on ALSA device %s.\n", context->audio_capture.capture_device_name);

    while (context->audio_capture.capturing) {
        err = snd_pcm_readi(context->audio_capture.handle, (char *)buffer, context->audio_capture.dsp_buffer_sample_count);

        //returns -EPIPE -> overrun
        //< 0 --> error
        if (err == -EPIPE) {
            if (debug) printf("Record audio buffer overrun.\n");
        }else if (err<0){
            fprintf(stderr, "error from read: %s\n", snd_strerror(err));
        } else {
            capture_process_frames(context, buffer, err);
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


//record_audio <device>,<sample_rate>,<sample_count>,<channels>,<gain>
//device: alsa device or udp://port
void record_audio(thread_context* context, char* args) {
    char device_name[MAX_VAL_LEN] = { 0 };
    int err;
    int channels=2; //record stereo
    unsigned int wanted_rate = 24000;
    unsigned int rate;
    unsigned int sample_count = 1024;
    float gain=1.0;

    strcpy(device_name, "default");

    ///snd_pcm_hw_params_t* hw_params;
    snd_pcm_format_t format = AUDIO_CAPTURE_FORMAT; //

    args = read_str(args, device_name, MAX_VAL_LEN);
    args = read_int(args, &wanted_rate);
    args = read_int(args, &channels);
    args = read_int(args, &sample_count);
    args = read_int(args, &channels);
    args = read_float(args, &gain);

    if (gain <= 0) gain = 1.0;

    rate = wanted_rate;

    if (strncmp(device_name, "udp://", 6) == 0 || strncmp(device_name, "udpb://", 6) == 0) {
        context->audio_capture.capture_mode = CAPTURE_MODE_UDP;
    } else if (strncmp(device_name, "pipe://", 7) == 0){
        context->audio_capture.capture_mode = CAPTURE_MODE_PIPE;
    } else {
        context->audio_capture.capture_mode = CAPTURE_MODE_ALSA;
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
    }

    context->audio_capture.capturing = 1;
    context->audio_capture.channel_count = channels;
    context->audio_capture.dsp_mode = DSP_MODE_NONE;
    context->audio_capture.dsp_buffer_sample_count = sample_count;
    context->audio_capture.rate = rate;
    context->audio_capture.format = format;
    context->audio_capture.capture_dst_buffer = NULL;
    context->audio_capture.capture_gain = gain;

    strcpy(context->audio_capture.capture_device_name, device_name);
    int s;
    switch (context->audio_capture.capture_mode) {
    case CAPTURE_MODE_ALSA:
        s = pthread_create(&context->audio_capture.thread, NULL, (void* (*)(void*)) & capture_thread_func_alsa, context);
        break;
    case CAPTURE_MODE_UDP:
        s = pthread_create(&context->audio_capture.thread, NULL, (void* (*)(void*)) & capture_thread_func_udp, context);
        break;
    case CAPTURE_MODE_PIPE:
        s = pthread_create(&context->audio_capture.thread, NULL, (void* (*)(void*)) & capture_thread_func_pipe, context);
        break;
    }
    
    if (s != 0) {
        fprintf(stderr, "Error creating new capture thread: %d.", s);
        snd_pcm_hw_params_free(context->audio_capture.hw_params);
        snd_pcm_close(context->audio_capture.handle);
        context->audio_capture.capturing = 0;
        perror(NULL);
    }

    if (debug) fprintf(stdout, "Capturing OK.\n");

}