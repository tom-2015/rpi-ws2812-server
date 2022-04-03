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

#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/v4l2-common.h>
#include <linux/v4l2-controls.h>
#include <linux/videodev2.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "jpeglib.h"
#include "jerror.h"
#include <setjmp.h>
//from https://github.com/LuaDist/libjpeg/blob/master/example.c
struct my_error_mgr {
    struct jpeg_error_mgr pub;	/* "public" fields */

    jmp_buf setjmp_buffer;	/* for return to caller */
};

typedef struct my_error_mgr* my_error_ptr;

/*
 * Here's the routine that will replace the standard error_exit method:
 */

METHODDEF(void) my_error_exit(j_common_ptr cinfo) {
    /* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
    my_error_ptr myerr = (my_error_ptr)cinfo->err;

    /* Always display the message. */
    /* We could postpone this until after returning, if we chose. */
    (*cinfo->err->output_message) (cinfo);

    /* Return control to the setjmp point */
    longjmp(myerr->setjmp_buffer, 1);
}

void decompress_jpg(char* buffer, int size);


int main(int argc, char* argv[]) {

    // 1.  Open the device
    int fd; // A file descriptor to the video device
    fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("Failed to open device, OPEN");
        return 1;
    }

    printf("1\n");

    // 2. Ask the device if it can capture frames
    struct v4l2_capability capability;
    if (ioctl(fd, VIDIOC_QUERYCAP, &capability) < 0) {
        // something went wrong... exit
        perror("Failed to get device capabilities, VIDIOC_QUERYCAP");
        return 1;
    }

    printf("2\n");

    // 3. Set Image format
    struct v4l2_format imageFormat;
    imageFormat.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    imageFormat.fmt.pix.width = 40;
    imageFormat.fmt.pix.height = 30;
    imageFormat.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    imageFormat.fmt.pix.field = V4L2_FIELD_NONE;
    // tell the device you are using this format
    if (ioctl(fd, VIDIOC_S_FMT, &imageFormat) < 0) {
        perror("Device could not set format, VIDIOC_S_FMT");
        return 1;
    }
    printf("3\n");

    // 4. Request Buffers from the device
    struct v4l2_requestbuffers requestBuffer = { 0 };
    requestBuffer.count = 1; // one request buffer
    requestBuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; // request a buffer wich we an use for capturing frames
    requestBuffer.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_REQBUFS, &requestBuffer) < 0) {
        perror("Could not request buffer from device, VIDIOC_REQBUFS");
        return 1;
    }

    printf("4\n");

    // 5. Quety the buffer to get raw data ie. ask for the you requested buffer
    // and allocate memory for it
    struct v4l2_buffer queryBuffer = { 0 };
    queryBuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    queryBuffer.memory = V4L2_MEMORY_MMAP;
    queryBuffer.index = 0;
    if (ioctl(fd, VIDIOC_QUERYBUF, &queryBuffer) < 0) {
        perror("Device did not return the buffer information, VIDIOC_QUERYBUF");
        return 1;
    }

    printf("5\n");
    // use a pointer to point to the newly created buffer
    // mmap() will map the memory address of the device to
    // an address in memory
    char* buffer = (char*)mmap(NULL, queryBuffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, queryBuffer.m.offset);
    memset(buffer, 0, queryBuffer.length);


    // 6. Get a frame
    // Create a new buffer type so the device knows whichbuffer we are talking about
    struct v4l2_buffer bufferinfo;
    memset(&bufferinfo, 0, sizeof(bufferinfo));
    bufferinfo.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    bufferinfo.memory = V4L2_MEMORY_MMAP;
    bufferinfo.index = 0;

    printf("6\n");

    // Activate streaming
    int type = bufferinfo.type;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("Could not start streaming, VIDIOC_STREAMON");
        return 1;
    }

    /***************************** Begin looping here *********************/

    int i;
    char filename[32];
    int frame_count = 10;

    for (i = 0;i < frame_count; i++) {
        printf("7\n");

        // Queue the buffer
        if (ioctl(fd, VIDIOC_QBUF, &bufferinfo) < 0) {
            perror("Could not queue buffer, VIDIOC_QBUF");
            return 1;
        }
        printf("8\n");
        // Dequeue the buffer
        if (ioctl(fd, VIDIOC_DQBUF, &bufferinfo) < 0) {
            perror("Could not dequeue the buffer, VIDIOC_DQBUF");
            return 1;
        }


        // Frames get written after dequeuing the buffer

        printf("Buffer has: %f KBytes of data\n", (double)bufferinfo.bytesused / 1024);

        FILE* outfile;

        sprintf(filename, "test_%d.jpg", i);

        if ((outfile = fopen(filename, "wb")) == NULL) {
            fprintf(stderr, "Error: can't open file\n");
            return 1;
        }

        fwrite(buffer, bufferinfo.bytesused, 1, outfile);

        fclose(outfile);

        printf("Decompress buffer\n");

        decompress_jpg(buffer, bufferinfo.bytesused);

    }
    // Write the data out to file
    /*ofstream outFile;
    outFile.open("webcam_output.jpeg", ios::binary | ios::app);

    int bufPos = 0, outFileMemBlockSize = 0;  // the position in the buffer and the amoun to copy from
                                        // the buffer
    int remainingBufferSize = bufferinfo.bytesused; // the remaining buffer size, is decremented by
                                                    // memBlockSize amount on each loop so we do not overwrite the buffer
    char* outFileMemBlock = NULL;  // a pointer to a new memory block
    int itr = 0; // counts thenumber of iterations
    while (remainingBufferSize > 0) {
        bufPos += outFileMemBlockSize;  // increment the buffer pointer on each loop
                                        // initialise bufPos before outFileMemBlockSize so we can start
                                        // at the begining of the buffer

        outFileMemBlockSize = 1024;    // set the output block size to a preferable size. 1024 :)
        outFileMemBlock = new char[sizeof(char) * outFileMemBlockSize];

        // copy 1024 bytes of data starting from buffer+bufPos
        memcpy(outFileMemBlock, buffer + bufPos, outFileMemBlockSize);
        outFile.write(outFileMemBlock, outFileMemBlockSize);

        // calculate the amount of memory left to read
        // if the memory block size is greater than the remaining
        // amount of data we have to copy
        if (outFileMemBlockSize > remainingBufferSize)
            outFileMemBlockSize = remainingBufferSize;

        // subtract the amount of data we have to copy
        // from the remaining buffer size
        remainingBufferSize -= outFileMemBlockSize;

        // display the remaining buffer size
        cout << itr++ << " Remaining bytes: " << remainingBufferSize << endl;

        delete outFileMemBlock;
    }

    // Close the file
    outFile.close();*/


    /******************************** end looping here **********************/

        // end streaming
    if (ioctl(fd, VIDIOC_STREAMOFF, &type) < 0) {
        perror("Could not end streaming, VIDIOC_STREAMOFF");
        return 1;
    }

    close(fd);


    return 0;
}

static void init_source(j_decompress_ptr cinfo) {}
static boolean fill_input_buffer(j_decompress_ptr cinfo)
{
    ERREXIT(cinfo, JERR_INPUT_EMPTY);
    return TRUE;
}
static void skip_input_data(j_decompress_ptr cinfo, long num_bytes)
{
    struct jpeg_source_mgr* src = (struct jpeg_source_mgr*)cinfo->src;

    if (num_bytes > 0) {
        src->next_input_byte += (size_t)num_bytes;
        src->bytes_in_buffer -= (size_t)num_bytes;
    }
}
static void term_source(j_decompress_ptr cinfo) {}

void decompress_jpg(char* in_buffer, int in_size) {
    struct jpeg_decompress_struct cinfo;
    struct my_error_mgr jerr;

    int row_stride;		/* physical row width in output buffer */

    // We set up the normal JPEG error routines, then override error_exit.
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;
    // Establish the setjmp return context for my_error_exit to use.
    if (setjmp(jerr.setjmp_buffer)) {
        /* If we get here, the JPEG code has signaled an error.
            * We need to clean up the JPEG object, close the input file, and return.
            */
        jpeg_destroy_decompress(&cinfo);
        return;
    }

    jpeg_create_decompress(&cinfo);

    //https://stackoverflow.com/questions/5280756/libjpeg-ver-6b-jpeg-stdio-src-vs-jpeg-mem-src
    struct jpeg_source_mgr* src = (struct jpeg_source_mgr*)(*cinfo.mem->alloc_small) ((j_common_ptr)&cinfo, JPOOL_PERMANENT, sizeof(struct jpeg_source_mgr));

    src->init_source = init_source;
    src->fill_input_buffer = fill_input_buffer;
    src->skip_input_data = skip_input_data;
    src->resync_to_restart = jpeg_resync_to_restart; /* use default method */
    src->term_source = term_source;
    src->bytes_in_buffer = in_size;
    src->next_input_byte = (JOCTET*)in_buffer;
    cinfo.src = src;

    // Now we can initialize the JPEG decompression object.
    
    //jpeg_stdio_src(&cinfo, infile);

    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);

    row_stride = cinfo.output_width * cinfo.output_components;

    JSAMPARRAY buffer;	// Output row buffer
    int i = 0, jpg_idx = 0, led_idx; //pixel index for current row, jpeg image, led string
    buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE, row_stride, 1);

    unsigned int * edge_buffer;
    malloc(edge_buffer, edge_width * 2 + edge_height * 2 * 3 * sizeof(unsigned int));

    int eofstring = 0;
    while (eofstring == 0 && cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, buffer, 1);
        int y = cinfo.output_scanline;
        for (i = 0;i < cinfo.image_width;i++) {
            int edge_x, edge_y, edge_index;
            edge_index = edge_x + edge_y;

            if (y < cinfo.image_height) {
                edge_buffer[i] = ; //add top row
            } else {
                edge_buffer[i + edge_height] = ; //add bottom row
            }

            if (x < cinfo.image_width) {
                edge_buffer[edge_width * 2 + edge_height * 2 - y] = ; //add left
            } else {
                edge_buffer[edge_width + y] = ; //add right
            }

           /* if (jpg_idx >= offset) { //check jpeg offset
                unsigned char r, g, b;
                r = buffer[0][i * cinfo.output_components];
                g = buffer[0][i * cinfo.output_components + 1];
                b = buffer[0][i * cinfo.output_components + 2];
                if (cinfo.output_components == 1) { //grayscale image
                    g = r;
                    b = r;
                }
                if (flip_rows) { //this will horizontaly flip the row
                    if (row_index & 1) {
                        led_idx = start + cinfo.image_width * row_index + (cinfo.image_width - i);
                    }
                }
                if (led_idx < start + len) {
                    if (debug) printf("led %d= r %d,g %d,b %d, jpg idx=%d\n", led_idx, r, g, b, jpg_idx);
                    int fill_color = color(r, g, b);
                    switch (op) {
                    case 0:
                        leds[led_idx].color = fill_color;
                        break;
                    case 1:
                        leds[i].color |= fill_color;
                        break;
                    case 2:
                        leds[i].color &= fill_color;
                        break;
                    case 3:
                        leds[i].color ^= fill_color;
                        break;
                    case 4:
                        leds[i].color = ~fill_color;
                        break;
                    }
                }
                led_idx++;
                if (led_idx >= start + len) {
                    if (delay != 0) {//reset led index if we are at end of led string and delay
                        led_idx = start;
                        row_index = 0;
                        render_channel(channel);
                        usleep(delay * 1000);
                    }
                    else {
                        eofstring = 1;
                        break;
                    }
                }*/
            }
           // if (context->end_current_command) break; //signal to exit this command
           // jpg_idx++;
        //}
        //row_index++;
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

}

/*int main() {
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

}*/

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

//screenshot
//https://gist.github.com/bozdag/9909679