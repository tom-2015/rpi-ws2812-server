#include "ambilight.h"
#include <ctype.h>
#include <linux/types.h>
#include <linux/v4l2-common.h>
#include <linux/v4l2-controls.h>
#include <linux/videodev2.h>
#include <linux/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <math.h>
#include "../jpghelper.h"

typedef struct {
    unsigned int r;
    unsigned int g;
    unsigned int b;
} uint_color;

//Creates ambilight effect
//ambilight channel, video_device, leds_x, leds_y, image_width, image_height, ambi_type, start, delay
void ambilight(thread_context* context, char* args) {
    char device_name[MAX_VAL_LEN] = { 0 };
    int channel = 0, image_width = 640, image_height = 360, leds_x = 0, leds_y=0, start=0, delay=10, ambi_type=1;

    args = read_channel(args, &channel);

    if (is_valid_channel_number(channel)) {

        strcpy(device_name, "/dev/video0");

        args = read_str(args, device_name, MAX_VAL_LEN);

        args = read_int(args, &leds_x);
        args = read_int(args, &leds_y);
        args = read_int(args, &image_width);
        args = read_int(args, &image_height);
        args = read_int(args, &ambi_type);
        args = read_int(args, &start);
        args = read_int(args, &delay);

        unsigned int total_leds = (leds_x + leds_y * 2); //total LEDs used for the ambilight, depend on the type
        if (ambi_type==2) total_leds += leds_x; //also LEDs on the bottom

        if ((get_led_count(channel)-start) < total_leds){
            fprintf(stderr, "Error: leds_x + leds_y * 2 > led_count, not enough LEDs in strip from start point. Expected total LEDs in your string: %d\n", total_leds);
            return;
        }

        if (debug) printf("ambilight %d,%d,%d,%d,%d,%d,%d,%d\n", channel, leds_x, leds_y, image_width, image_height, ambi_type, start, delay);

        // 1.  Open the device
        int fd; // A file descriptor to the video device
        fd = open(device_name, O_RDWR);
        if (fd < 0) {
            perror("Failed to open video device.");
            close(fd);
            return;
        }

        // 2. Ask the device if it can capture frames
        struct v4l2_capability capability;
        if (ioctl(fd, VIDIOC_QUERYCAP, &capability) < 0) {
            // something went wrong... exit
            perror("Failed to get device capabilities, VIDIOC_QUERYCAP");
            close(fd);
            return;
        }

        // 3. Set Image format
        struct v4l2_format imageFormat;
        imageFormat.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        imageFormat.fmt.pix.width = image_width;
        imageFormat.fmt.pix.height = image_height;
        imageFormat.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
        imageFormat.fmt.pix.field = V4L2_FIELD_NONE;

        // tell the device you are using this format
        if (ioctl(fd, VIDIOC_S_FMT, &imageFormat) < 0) {
            perror("Device could not set format, VIDIOC_S_FMT");
            close(fd);
            return;
        }

        // 4. Request Buffers from the device
        struct v4l2_requestbuffers requestBuffer = { 0 };
        requestBuffer.count = 1; // one request buffer
        requestBuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; // request a buffer wich we an use for capturing frames
        requestBuffer.memory = V4L2_MEMORY_MMAP;

        if (ioctl(fd, VIDIOC_REQBUFS, &requestBuffer) < 0) {
            perror("Could not request buffer from device, VIDIOC_REQBUFS");
            close(fd);
            return;
        }

        // 5. Quety the buffer to get raw data ie. ask for the you requested buffer
        // and allocate memory for it
        struct v4l2_buffer queryBuffer = { 0 };
        queryBuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        queryBuffer.memory = V4L2_MEMORY_MMAP;
        queryBuffer.index = 0;
        if (ioctl(fd, VIDIOC_QUERYBUF, &queryBuffer) < 0) {
            perror("Device did not return the buffer information, VIDIOC_QUERYBUF");
            close(fd);
            return;
        }

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

        // Activate streaming
        int type = bufferinfo.type;
        if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
            perror("Could not start streaming, VIDIOC_STREAMON");
            close(fd);
            return;
        }

        //initialize JPEG decompression object
        struct jpeg_decompress_struct cinfo;
        struct my_error_mgr jerr;

        int row_stride;		/* physical row width in output buffer */

        // We set up the normal JPEG error routines, then override error_exit.
        cinfo.err = jpeg_std_error(&jerr.pub);
        jerr.pub.error_exit = my_error_exit;
        // Establish the setjmp return context for my_error_exit to use.
        if (setjmp(jerr.setjmp_buffer)) {
            // If we get here, the JPEG code has signaled an error.  We need to clean up the JPEG object, close the input file, and return.
            jpeg_destroy_decompress(&cinfo);
            close(fd);
            return;
        }

        jpeg_create_decompress(&cinfo);
        struct jpeg_source_mgr* src = (struct jpeg_source_mgr*)(*cinfo.mem->alloc_small) ((j_common_ptr)&cinfo, JPOOL_PERMANENT, sizeof(struct jpeg_source_mgr));

        src->init_source = jpg_init_source;
        src->fill_input_buffer = jpg_fill_input_buffer;
        src->skip_input_data = jpg_skip_input_data;
        src->term_source = jpg_term_source;
        src->resync_to_restart = jpeg_resync_to_restart; /* use default method */
        src->bytes_in_buffer = 0;
        src->next_input_byte = (JOCTET*)buffer;
        cinfo.src = src;

        uint_color * avg_colors = (uint_color *) malloc(total_leds * sizeof(uint_color)); //holds total average colors
        unsigned int * color_count = (unsigned int *) malloc(total_leds * sizeof(unsigned int)); //holds total color values
        ws2811_led_t * leds = get_led_string(channel); //led string 
        leds+=start; //start at this offset

        while (!context->end_current_command) {
            // Queue the buffer
            if (ioctl(fd, VIDIOC_QBUF, &bufferinfo) < 0) {
                perror("Could not queue buffer, VIDIOC_QBUF");
                close(fd);
                jpeg_destroy_decompress(&cinfo);
                return;
            }

            if (debug) printf("Get Frame.\n");
            
            if (delay>0) usleep(delay * 1000);
            
            // Dequeue the buffer
            if (ioctl(fd, VIDIOC_DQBUF, &bufferinfo) < 0) {
                perror("Could not dequeue the buffer, VIDIOC_DQBUF");
                close(fd);
                jpeg_destroy_decompress(&cinfo);
                return;
            }

            //tell JPEG how many bytes received and the input buffer location
            src->bytes_in_buffer = bufferinfo.bytesused;
            src->next_input_byte = (JOCTET*)buffer;

            if (src->next_input_byte[0] == 0xFF && src->next_input_byte[1] == 0xD8 && src->next_input_byte[2] == 0xFF) {

                if (debug) printf("JPEG header OK\n");
                //start read JPEG
                jpeg_read_header(&cinfo, TRUE);
                jpeg_start_decompress(&cinfo);

                if (cinfo.image_width > 0 && cinfo.image_height > 0) {

                    if (debug) printf("JPG decompressed, size: %dx%d.\n", cinfo.image_width, cinfo.image_height);

                    double x_scale= (double)leds_x / (double)cinfo.image_width; //scale for projecting the picture on the ledstrip
                    double y_scale= (double)leds_y / (double)cinfo.image_height;

                    JSAMPARRAY jpg_buffer;	// Output row buffer
                    int jpg_row_stride = cinfo.output_width * cinfo.output_components; //number of bytes in 1 row of the jpg image
                    jpg_buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE, jpg_row_stride, 1); //pointer to pixel data from jpg

                    unsigned int i;
                    unsigned int row_index = 0;
                    
                    //clear colors
                    memset(avg_colors, 0, total_leds *  sizeof(uint_color));
                    memset(color_count, 0, total_leds * sizeof(unsigned int));

                    while (cinfo.output_scanline < cinfo.output_height) {
                        jpeg_read_scanlines(&cinfo, jpg_buffer, 1);
                        for (i = 0;i < cinfo.image_width;i++) {
                            unsigned int jpg_base_index = i * cinfo.output_components;
                            unsigned int base_index;

                            if (i < cinfo.image_width / 2){//projection left
                                base_index = floor(y_scale * (double) (cinfo.image_height - 1 - row_index));
                                avg_colors[base_index].r += jpg_buffer[0][jpg_base_index];
                                avg_colors[base_index].g += jpg_buffer[0][jpg_base_index + 1];
                                avg_colors[base_index].b += jpg_buffer[0][jpg_base_index + 2];
                                color_count[base_index]++;
                            }else{ //projection right
                                base_index = floor(y_scale * (double) row_index) + leds_y + leds_x;
                                avg_colors[base_index].r += jpg_buffer[0][jpg_base_index];
                                avg_colors[base_index].g += jpg_buffer[0][jpg_base_index + 1];
                                avg_colors[base_index].b += jpg_buffer[0][jpg_base_index + 2];
                                color_count[base_index]++;
                            }

                            if (row_index < cinfo.image_height / 2){ //projection top
                                base_index = floor(x_scale * (double) i) + leds_y;
                                avg_colors[base_index].r += jpg_buffer[0][jpg_base_index];
                                avg_colors[base_index].g += jpg_buffer[0][jpg_base_index + 1];
                                avg_colors[base_index].b += jpg_buffer[0][jpg_base_index + 2];
                                color_count[base_index]++;
                            }else if (ambi_type==2){ //projection bottom, optional
                                base_index = floor(x_scale * (double) (cinfo.image_width - 1 - i)) + leds_y * 2 + leds_x;
                                avg_colors[base_index].r += jpg_buffer[0][jpg_base_index];
                                avg_colors[base_index].g += jpg_buffer[0][jpg_base_index + 1];
                                avg_colors[base_index].b += jpg_buffer[0][jpg_base_index + 2];
                                color_count[base_index]++;                       
                            }
                        }
                        row_index++;
                    }

                    //now transfer to ledstring
                    for (i=0;i<total_leds;i++){
                        if (color_count[i]!=0)
                            leds[i].color = color(avg_colors[i].r / color_count[i], avg_colors[i].g / color_count[i], avg_colors[i].b / color_count[i]);
                    }

                    if (debug) printf("Rendering ambilight.\n");
                    render_channel(channel);
                }
                jpeg_finish_decompress(&cinfo);
            }
        }

        free (avg_colors);
        free (color_count);
        jpeg_destroy_decompress(&cinfo);

    }else {
        fprintf(stderr, ERROR_INVALID_2D_CHANNEL);
    }
}
