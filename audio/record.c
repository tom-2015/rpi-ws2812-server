#include "record.h"
#include <pthread.h>
#include <math.h> 
#include "../sockets.h"
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <bits/fcntl-linux.h>

//TO DO: use neon for some faster signal processing?
//#include <arm_neon.h>
//https://developer.arm.com/architectures/instruction-sets/intrinsics/#f:@navigationhierarchiessimdisa=%5BNeon%5D&q=fmul&first=20

#define F_GETPIPE_SZ 1032

//counts number of thresholds reached
void process_threshold(thread_context * context, float* buffer, unsigned int len) {
    unsigned int i;
    unsigned int threshold_reached = 0;
    unsigned int num_samples = context->audio_capture.dsp_buffer_sample_count * context->audio_capture.channel_count;
    float threshold = context->audio_capture.threshold;

    /*if (true || context->audio_capture.auto_threshold){
        threshold=0;
        float max = 0;
        for (i=0; i < len; i++){
            if (fabs(buffer[i])> max) max = fabs(buffer[i]);
            //threshold+= fabs(buffer[i]);
        }
       //threshold = threshold / len * 2;
        threshold = max * 2 / 5;
        //threshold = threshold * 0.99;
    }*/

    //printf("%f\n", threshold);

    if (threshold > 0.0000001){
        for (i = 0; i < len;i++) { //context->audio_capture.dsp_buffer_sample_count * context->audio_capture.channel_count
            if (fabs(buffer[i]) >= threshold) {
                threshold_reached++;
            }
        }
    }
    
    pthread_mutex_lock(&context->audio_capture.buffer_mutex);
    *((unsigned int*)context->audio_capture.capture_dst_buffer) += threshold_reached;
    context->audio_capture.capture_dst_buffer_count += context->audio_capture.dsp_buffer_sample_count;
    pthread_mutex_unlock(&context->audio_capture.buffer_mutex);
}

/*inline float map_level(float x, float in_min, float in_max, float out_min, float out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}


float filter(float cutofFreq){
    float RC = 1.0/(cutofFreq * 2 * M_PI);
    float dt = 1.0/SAMPLE_RATE;
    float alpha = dt/(RC+dt);

    return alpha;
}*/


void process_avg(thread_context* context, float* buffer, unsigned int len) {
    unsigned int i;
    float res = 0.0;
    static float test_avg = 0;
    static float max = 0;
    //static float abs_val;

    //max = max * 0.99;
    /*max = max * 0.9;
    for (i = 0; i < len; i++) {
        res += fabs(buffer[i]);
    }
    res = res / len; //(float)context->audio_capture.dsp_buffer_sample_count;

    if (res > max){
        max = res;
    }

    test_avg = (test_avg + res) / 2.0f;*/

    /*float coef = 0.049754726188;

    for (i = 0; i < len; i++) {
        buffer[i] = prev + (coef * buffer[i] - prev);
        prev = buffer[i];
    }*/

    for (i = 0; i < len; i++) {
        //abs_val = ;
        res += fabs(buffer[i]);
        //if (fabs(buffer[i])>max) max = fabs(buffer[i]);
    }
    res = res / len;
//0.000061
//0.000304
    ///if (max / res < 8) /*0.0004)*/ res = 0;
    //printf("%f, ", max);
    pthread_mutex_lock(&context->audio_capture.buffer_mutex);
    *((float*)context->audio_capture.capture_dst_buffer) = res * 1.0; // / max;
    pthread_mutex_unlock(&context->audio_capture.buffer_mutex);
}

void low_pass_pre_filter(thread_context* context, float* buffer, unsigned int len){
    unsigned int i;
    float coef;
    static float prev_buff[MAX_THREADS+1] = {};
    float prev, prev_sample, tmp_sample;
    
    prev = prev_buff[context->id];
    coef =  context->audio_capture.low_pass_filter_coef; // 0.049754726188;
    
    //y[i] := y[i-1] + α * (x[i] - y[i-1])
    for (i = 0; i < len; i++) {
        buffer[i] = prev + coef * (buffer[i] - prev);
        prev = buffer[i];
    }
    
    prev_buff[context->id]=prev;
    //printf("low pass pre filter");
}

void high_pass_pre_filter(thread_context* context, float* buffer, unsigned int len){
    unsigned int i;
    float coef;
    static float prev_buff[MAX_THREADS+1] = {};
    static float prev_samples[MAX_THREADS+1]={};
    float prev, prev_sample, tmp_sample;

    prev = prev_buff[context->id];
    coef =  context->audio_capture.high_pass_filter_coef;
    prev_sample = prev_samples[context->id];

    //y[i] := α × (y[i−1] + x[i] − x[i−1])
    for (i = 0;i < len; i++){
        tmp_sample = buffer[i]; //store current sample value
        prev = coef * (prev + buffer[i] - prev_sample); //calculate store as previous output value (y[i-1])
        buffer[i] = prev; //store calculated
        prev_sample = tmp_sample; //copy prev sample (x[i-1])
    }
    prev_buff[context->id]=prev; //store for next function call
    prev_samples[context->id]=prev_sample;
}

inline void process_pre_filter(thread_context* context, float* buffer, unsigned int len){
    switch (context->audio_capture.filter_mode){
        case FILTER_MODE_NONE:
            return;
            break;
        case FILTER_MODE_LOW_PASS:
            low_pass_pre_filter(context, buffer, len);
            break;
        case FILTER_MODE_HIGH_PASS:
            high_pass_pre_filter(context, buffer, len);
            break;            
        case FILTER_MODE_BAND_PASS:
            low_pass_pre_filter(context, buffer, len);
            high_pass_pre_filter(context, buffer, len);
            break;
    }
}

void capture_process_frames(thread_context* context, float* buffer, unsigned int len) {
    static int i=0;
    int j;
    float gain = context->audio_capture.capture_gain;
    
    if (context->audio_capture.capture_gain != 1.0) {
        for (j = 0; j < len; j++) {
            buffer[j] = buffer[j] * gain;
            if (buffer[j] > 1.0) buffer[j] = 1.0;
            else if (buffer[j] < -1.0) buffer[j] = -1.0;
        }
    }

    if (context->audio_capture.filter_mode!=FILTER_MODE_NONE) process_pre_filter(context, buffer, len); //send samples through a filter first

    //now pass samples to the DSP code that will generate the effect for the leds
    switch (context->audio_capture.dsp_mode) {
    case DSP_MODE_NONE:
        if (debug && (i >= context->audio_capture.rate)) {
            float avg = 0;
            for (j = 0;j < len ;j++) avg += fabs(buffer[j]);
            avg = avg / (float)i;
            printf("Processed %d audio samples (%f, %d).\n", i, avg, len);
            i = 0;
        } else {
            i += len;
        }
        break;
    case DSP_MODE_THRESHOLD: //
        process_threshold(context, buffer, len);
        break;
    case DSP_MODE_AVG:
        process_avg(context, buffer, len);
        break;
    }

    for(j=0;j<MAX_THREADS;j++){
        thread_context * thread = get_thread(j);
        //printf("Check thread: j=%d, running=%d, capture_mode=%d, copy_from=%d, context=%d\n", j, thread->thread_running, thread->audio_capture.capture_mode, thread->audio_capture.copy_from_thread, context->id);
        if (thread->thread_running && thread->audio_capture.capture_mode == CAPTURE_MODE_OTHER_THREAD && thread->audio_capture.capturing && thread->audio_capture.copy_from_thread == context->id){
            //forward samples to other thread
            pipe_producer_t * writer = (pipe_producer_t*) thread->audio_capture.sample_pipe_writer;
            if (writer!=NULL){
                pipe_push(writer, buffer, len);
            }
        }
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


        if (debug){
            int pipe_size;
            int sz;

            pipe_size = fcntl(input_file->_fileno, F_GETPIPE_SZ);
            printf("Pipe size: %d\n", pipe_size);
            
            ioctl(input_file->_fileno, FIONREAD, &sz);
            printf("Remaining pipe buffer: %d\n", sz);
        }

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
    size_t buffer_size = context->audio_capture.dsp_buffer_sample_count * snd_pcm_format_width(AUDIO_CAPTURE_FORMAT) / 8 * context->audio_capture.channel_count;
    
    buffer = (float*)malloc(buffer_size);

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
            capture_process_frames(context, buffer, err * context->audio_capture.channel_count);
        }
    }

    free(buffer);

    snd_pcm_hw_params_free(context->audio_capture.hw_params);

    snd_pcm_close(context->audio_capture.handle);
    if (debug) fprintf(stdout, "Audio interface closed.\n");
}

void capture_thread_func_thread(thread_context* context) {
    float* buffer;
    size_t samples_read;
    
    buffer = (float*)malloc(context->audio_capture.dsp_buffer_sample_count * snd_pcm_format_width(AUDIO_CAPTURE_FORMAT) / 8 * context->audio_capture.channel_count);

    if (debug) printf("Capturing/copy audio from other thread %d, %d, %d.\n", context->audio_capture.copy_from_thread, context->audio_capture.sample_pipe_reader, context->audio_capture.sample_pipe_writer);

    samples_read = pipe_pop((pipe_consumer_t *) context->audio_capture.sample_pipe_reader, buffer, context->audio_capture.dsp_buffer_sample_count * context->audio_capture.channel_count);
    while (context->audio_capture.capturing && samples_read > 0) {
        capture_process_frames(context, buffer, samples_read);
        samples_read = pipe_pop((pipe_consumer_t * )context->audio_capture.sample_pipe_reader, buffer, context->audio_capture.dsp_buffer_sample_count * context->audio_capture.channel_count);
    }

    if (debug) printf("End of capture thread function.\n");
    context->audio_capture.capturing = 0;
    free(buffer);
    if (context->audio_capture.sample_pipe!=NULL) pipe_free((pipe_t *)context->audio_capture.sample_pipe);
    if (context->audio_capture.sample_pipe_reader!=NULL) pipe_consumer_free((pipe_consumer_t *)context->audio_capture.sample_pipe_reader);
    if (context->audio_capture.sample_pipe_writer!=NULL) pipe_producer_free((pipe_producer_t *)context->audio_capture.sample_pipe_writer);
    context->audio_capture.sample_pipe = NULL;
    context->audio_capture.sample_pipe_writer = NULL;
    context->audio_capture.sample_pipe_reader = NULL;
}

void stop_record(thread_context* context, char* args) {
    int j;

    context->audio_capture.capturing = 0;

    for(j=0;j<MAX_THREADS;j++){
        thread_context * thread = get_thread(j);
        //tell other threads that are reading our samples that we will not produce anything anymore and exit
        if (thread->thread_running && thread->audio_capture.capture_mode == CAPTURE_MODE_OTHER_THREAD && thread->audio_capture.copy_from_thread == context->id){
            if (thread->audio_capture.sample_pipe!=NULL) pipe_free((pipe_t *) thread->audio_capture.sample_pipe);
            if (thread->audio_capture.sample_pipe_writer!=NULL) pipe_producer_free((pipe_producer_t *)thread->audio_capture.sample_pipe_writer);   
            thread->audio_capture.sample_pipe = NULL;
            thread->audio_capture.sample_pipe_writer = NULL;
            thread->audio_capture.capturing = 0;
        }
    }


    int res = pthread_join(context->audio_capture.thread, NULL); //wait for thread to finish, clean up and exit
    if (res != 0) {
        fprintf(stderr, "Error join thread in stop_record: %d, %d ", context->id, res);
        perror(NULL);
    }

}


//record_audio <device>,<sample_rate>,<channels>,<sample_count>,<gain>
//device: alsa device or udp://port
//from other thread:   thread://1
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
    args = read_float(args, &gain);

    if (gain <= 0) gain = 1.0;

    rate = wanted_rate;

    context->audio_capture.capture_mode = 0;
    context->audio_capture.sample_pipe = NULL;
    context->audio_capture.sample_pipe_reader = NULL;
    context->audio_capture.sample_pipe_writer = NULL;

    if (strncmp(device_name, "udp://", 6) == 0 || strncmp(device_name, "udpb://", 6) == 0) {
        context->audio_capture.capture_mode = CAPTURE_MODE_UDP;
    } else if (strncmp(device_name, "pipe://", 7) == 0){
        context->audio_capture.capture_mode = CAPTURE_MODE_PIPE;
    } else if (strncmp(device_name, "thread://", 9) == 0){
        if (strlen(device_name)>9){
            int thread_id = atoi(&device_name[9]);

            thread_context * capture_from = get_thread(thread_id); 

            if (!capture_from->audio_capture.capturing){
                fprintf(stderr, "Error target thread is not capturing audio! Please run the record_audio command on the target thread first.\n");
                return;
            }

            context->audio_capture.sample_pipe = (volatile pipe_t *)pipe_new(snd_pcm_format_width(AUDIO_CAPTURE_FORMAT) / 8, rate * 2 * capture_from->audio_capture.channel_count); //store max 2 sec of audio
            context->audio_capture.sample_pipe_reader = (volatile pipe_consumer_t *) pipe_consumer_new((pipe_t *) context->audio_capture.sample_pipe);
            context->audio_capture.sample_pipe_writer = (volatile pipe_producer_t *) pipe_producer_new((pipe_t *) context->audio_capture.sample_pipe);
            
            if (debug) printf("Set pipe for thread %d, %d, %d, %d\n", context->id, context->audio_capture.sample_pipe, context->audio_capture.sample_pipe_reader, context->audio_capture.sample_pipe_writer);
            if (debug) printf("Copy audio samples from thread %d.\n", thread_id);

            context->audio_capture.capture_mode = CAPTURE_MODE_OTHER_THREAD;
            context->audio_capture.copy_from_thread = thread_id;

            channels = capture_from->audio_capture.channel_count;
            sample_count = capture_from->audio_capture.dsp_buffer_sample_count;
            rate = capture_from->audio_capture.rate;
            format = capture_from->audio_capture.format;

        }else{
            fprintf(stderr, "Error unknown thread ID, provide the thread number you want to copy samples from after thread://.\n");
        }
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

    
    context->audio_capture.channel_count = channels;
    context->audio_capture.dsp_mode = DSP_MODE_NONE;
    context->audio_capture.dsp_buffer_sample_count = sample_count;
    context->audio_capture.rate = rate;
    context->audio_capture.format = format;
    context->audio_capture.capture_dst_buffer = NULL;
    context->audio_capture.capture_gain = gain;
    context->audio_capture.capturing = 1;
    ///context->audio_capture.auto_threshold = false;

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
    case CAPTURE_MODE_OTHER_THREAD:
        s = pthread_create(&context->audio_capture.thread, NULL, (void* (*)(void*)) & capture_thread_func_thread, context);
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

//filter_audio <mode>,<low_freq>,<high_freq>
void filter_audio(thread_context* context, char* args){
    int mode=FILTER_MODE_NONE;
    int low_freq=0;
    int high_freq=0;
    float RC,dt;

    args = read_int(args, &mode);
    args = read_int(args, &low_freq);
    args = read_int(args, &high_freq);

    switch (mode){
        case FILTER_MODE_BAND_PASS:
            RC = 1.0/(low_freq * 2 * M_PI);
            dt = 1.0/context->audio_capture.rate;
            context->audio_capture.high_pass_filter_coef = RC/(RC + dt);

            RC = 1.0 / (high_freq * 2 * M_PI);
            dt = 1.0 / context->audio_capture.rate;
            context->audio_capture.low_pass_filter_coef = dt/(RC+dt);
            context->audio_capture.filter_mode = FILTER_MODE_BAND_PASS;
            if (debug) printf("Filtering audio samples band pass %dHz-%dHz, coef low pass %f, coef high pass %d.\n", low_freq, high_freq, context->audio_capture.low_pass_filter_coef, context->audio_capture.high_pass_filter_coef);
            break;
        case FILTER_MODE_HIGH_PASS:
            RC = 1.0/(high_freq * 2 * M_PI);
            dt = 1.0/context->audio_capture.rate;
            context->audio_capture.high_pass_filter_coef = RC/(RC + dt);
            context->audio_capture.filter_mode = FILTER_MODE_HIGH_PASS;
            if (debug) printf("Filtering audio samples high pass %dHz, coef %f.\n", high_freq, context->audio_capture.high_pass_filter_coef);
            break;
        case FILTER_MODE_LOW_PASS:
            RC = 1.0 / (low_freq * 2 * M_PI);
            dt = 1.0 / context->audio_capture.rate;
            context->audio_capture.low_pass_filter_coef = dt/(RC+dt);
            context->audio_capture.filter_mode = FILTER_MODE_LOW_PASS;
            if (debug) printf("Filtering audio samples low pass %dHz, coef %f.\n", low_freq, context->audio_capture.low_pass_filter_coef);
            break;
        case FILTER_MODE_NONE:
            context->audio_capture.filter_mode = FILTER_MODE_NONE;
            break;
        default:
            context->audio_capture.filter_mode = FILTER_MODE_NONE;
            fprintf(stderr, "Error: unknown filter mode %d.\n", mode);
            break;
    }

}