#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <math.h> 
#include <alsa/asoundlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>


int main() {
    float* buffer;
    int num_samples = 1024;
    char named_pipe_file [256];
    int i = 0;
    FILE* input_file;
    // pipe://

    strcpy(named_pipe_file, "/dev/audio_in");

    remove(named_pipe_file);
    mkfifo(named_pipe_file, 0777);
    chmod(named_pipe_file, 0777);
    input_file = fopen(named_pipe_file, "r");

    buffer = (float*)malloc(sizeof(float) * num_samples);
    while (1) {

        if (fread((void*)buffer, sizeof(float), num_samples, input_file) != num_samples) {
            fprintf(stderr, "Error reading named pipe %s.\n", named_pipe_file);
            break;
        }

        float sum = 0;
        for (i = 0;i < num_samples;i++) {
            sum += fabs(buffer[i]);
        }

        printf("sum: %f\n", sum);

    }

    fclose(input_file);
    remove(named_pipe_file);

}

/*#define AUDIO_CAPTURE_FORMAT SND_PCM_FORMAT_FLOAT_LE

int main() {
    ///snd_pcm_hw_params_t* hw_params;
    unsigned int rate;
    int err;
    snd_pcm_t* handle;
    snd_pcm_hw_params_t* hw_params;
    unsigned int channel_count = 2;
    unsigned int wanted_rate = 24000;
    static snd_output_t* log;

    snd_pcm_format_t format = AUDIO_CAPTURE_FORMAT; //

    rate = wanted_rate;
    if ((err = snd_pcm_open(&handle, "plughw:CARD=Capture,DEV=0", SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        fprintf(stderr, "Cannot open audio device (%s).\n", snd_strerror(err));
        return -1;
    }

    fprintf(stdout, "Audio interface opened.\n");

    if ((err = snd_pcm_hw_params_malloc(&hw_params)) < 0) {
        fprintf(stderr, "Cannot allocate hardware parameter structure (%s).\n", snd_strerror(err));
        return -1;
    }

    fprintf(stdout, "hw_params allocated\n");

    if ((err = snd_pcm_hw_params_any(handle, hw_params)) < 0) {
        fprintf(stderr, "Cannot initialize hardware parameter structure (%s).\n", snd_strerror(err));
        return -1;
    }

    fprintf(stdout, "hw_params initialized\n");

    if ((err = snd_pcm_hw_params_set_access(handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        fprintf(stderr, "Cannot set access type (%s).\n", snd_strerror(err));
        return -1;
    }

    fprintf(stdout, "hw_params access setted\n");

    if ((err = snd_pcm_hw_params_set_format(handle, hw_params, format)) < 0) {
        fprintf(stderr, "Cannot set sample format (%s).\n", snd_strerror(err));
        return -1;
    }

    fprintf(stdout, "hw_params format setted\n");

    if ((err = snd_pcm_hw_params_set_rate_near(handle, hw_params, &rate, 0)) < 0) {
        fprintf(stderr, "Cannot set sample rate (%s).\n", snd_strerror(err));
        return -1;
    }

    if (rate != wanted_rate) {
        fprintf(stderr, "Warning wanted sample rate of %d not available, using %d.\n", wanted_rate, rate);
    }

    fprintf(stdout, "hw_params rate setted\n");

    if ((err = snd_pcm_hw_params_set_channels(handle, hw_params, channel_count)) < 0) {
        fprintf(stderr, "cannot set channel count (%s)\n", snd_strerror(err));
        return -1;
    }

    fprintf(stdout, "hw_params channels setted\n");

    if ((err = snd_pcm_hw_params(handle, hw_params)) < 0) {
        fprintf(stderr, "Cannot set parameters (%s).\n", snd_strerror(err));
        return -1;
    }

    snd_pcm_sw_params_t* swparams;
    snd_pcm_sw_params_alloca(&swparams);

    err = snd_pcm_sw_params_current(handle, swparams);
    if (err < 0) {
        printf("Unable to get current sw params.");
    }

    if (snd_pcm_sw_params(handle, swparams) < 0) {
        printf("Unable to set sw params.");
    }

    if ((err = snd_pcm_prepare(handle)) < 0) {
        fprintf(stderr, "Cannot prepare audio interface for use (%s).\n", snd_strerror(err));
        return -1;
    }

    fprintf(stdout, "Audio interface prepared.\n");

    snd_output_stdio_attach(&log, stderr, 0);
    snd_pcm_hw_params_dump(hw_params, log);
    snd_pcm_dump(handle, log);

    char* buffer;
    int i;
    float* fbuffer;
    buffer = (char *) malloc(1024 * snd_pcm_format_width(AUDIO_CAPTURE_FORMAT) / 8 * channel_count);
    fbuffer = (float*)buffer;
    unsigned int read = 0;

    while (1) {
        err = snd_pcm_readi(handle, buffer, channel_count);

        if (err == -EAGAIN ) {
            //if (!test_nowait)
            //    snd_pcm_wait(handle, 100);
            printf("EAGAIN\n");
        } else if (err == -EPIPE) {
            printf("EPIPE\n");
        } else if (err == -ESTRPIPE) {
            printf("ESTRPIPE\n");
        } else if (err < 0) {
            printf("read error: %s\n", snd_strerror(err));
        }
        if (err > 0) {
            float max = 0;
            for (i = 0;i < 1024;i++) {
                if (abs(fbuffer[i]) > max) max = abs(fbuffer[i]);
            }
            if (max > 0) {
                printf("max: %f\n", max);
            }
            read += err;
            if (read == rate) {
                printf("%d\n", read);
                read = 0;
            }
        }
    }

}*/