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

#include "../ws2812svr.h"
#include "../jpghelper.h"

//renders camera output (requires USE_JPEG)
//camera <channel>,<camera_device>,<dst_x>,<dst_y>,<dst_width>,<dst_height>,<frame_count>,<delay>
void camera(thread_context* context, char* args) {
    char device_name[MAX_VAL_LEN] = { 0 };
    int channel = 0, frame_count=0, delay=10;
    double dst_x = 0.0, dst_y = 0.0, dst_width = 0.0, dst_height = 0.0;

    args = read_channel(args, &channel);

    if (is_valid_2D_channel_number(channel)) {
        channel_info * led_channel = get_channel(channel);

        strcpy(device_name, "/dev/video0");

        args = read_str(args, device_name, MAX_VAL_LEN);
        
        dst_width = led_channel->width;
        dst_height = led_channel->height;
        
        args = read_double(args, &dst_x);
        args = read_double(args, &dst_y);
        args = read_double(args, &dst_width);
        args = read_double(args, &dst_height);
        args = read_int(args, &frame_count);
        args = read_int(args, &delay);

        if (debug) printf("camera %d\n", channel);


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
        imageFormat.fmt.pix.width = dst_width;
        imageFormat.fmt.pix.height = dst_height;
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

        //create cairo objects
        cairo_surface_t* src_surface=NULL;
        cairo_t* cr = led_channel->cr;

        cairo_save(cr);
        
        unsigned int frame_number = 0;
        const int cairo_pixel_size = 4; //number of bytes for each cairo pixel
        unsigned char* surface_pixels = NULL;
        unsigned int cairo_stride = 0;
        double xScale = 1;
        double yScale = 1;
        unsigned int src_x = 0, src_y = 0;

        while (!context->end_current_command && (frame_number < frame_count || frame_count == 0)) {
            // Queue the buffer
            if (ioctl(fd, VIDIOC_QBUF, &bufferinfo) < 0) {
                perror("Could not queue buffer, VIDIOC_QBUF");
                close(fd);
                jpeg_destroy_decompress(&cinfo);
                if (src_surface != NULL)  cairo_surface_destroy(src_surface);
                return;
            }

            if (debug) printf("Get Frame.\n");
            
            usleep(delay * 1000);
            
            // Dequeue the buffer
            if (ioctl(fd, VIDIOC_DQBUF, &bufferinfo) < 0) {
                perror("Could not dequeue the buffer, VIDIOC_DQBUF");
                close(fd);
                jpeg_destroy_decompress(&cinfo);
                if (src_surface != NULL)  cairo_surface_destroy(src_surface);
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
                    if (src_surface == NULL) {
                        src_surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, cinfo.image_width, cinfo.image_height);
                        surface_pixels = cairo_image_surface_get_data(src_surface); //get pointer to pixel data in cairo
                        cairo_stride = cairo_image_surface_get_stride(src_surface); //number of bytes in 1 row of cairo pointer
                        xScale = dst_width / (float)cinfo.image_width;
                        yScale = dst_height / (float)cinfo.image_height;
                        cairo_scale(cr, xScale, yScale);
                        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
                    }

                    if (debug) printf("JPG decompressed.\n");

                    JSAMPARRAY jpg_buffer;	// Output row buffer
                    int jpg_row_stride = cinfo.output_width * cinfo.output_components; //number of bytes in 1 row of the jpg image
                    jpg_buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE, jpg_row_stride, 1); //pointer to pixel data from jpg

                    unsigned int i;
                    unsigned int row_index = 0;
                    while (cinfo.output_scanline < cinfo.output_height) {
                        jpeg_read_scanlines(&cinfo, jpg_buffer, 1);
                        for (i = 0;i < cinfo.image_width;i++) {
                            unsigned char r, g, b;
                            r = jpg_buffer[0][i * cinfo.output_components];
                            g = jpg_buffer[0][i * cinfo.output_components + 1];
                            b = jpg_buffer[0][i * cinfo.output_components + 2];
                            if (cinfo.output_components == 1) { //grayscale image
                                g = r;
                                b = r;
                            }
                            surface_pixels[row_index * cairo_stride + i * cairo_pixel_size] = b;
                            surface_pixels[row_index * cairo_stride + i * cairo_pixel_size + 1] = g;
                            surface_pixels[row_index * cairo_stride + i * cairo_pixel_size + 2] = r;
                            //surface_pixels[row_index * cairo_stride + i * cairo_pixel_size + 3] = 0xFF; //alpha
                        }
                        row_index++;
                    }

                    //now paint the frame
                    cairo_surface_mark_dirty_rectangle(src_surface, 0, 0, cinfo.image_width, cinfo.image_height);
                    cairo_set_source_surface(cr, src_surface, (dst_x / xScale - src_x), (dst_y / yScale - src_y));
                    cairo_rectangle(cr, dst_x / xScale, dst_y / yScale, dst_width / xScale, dst_height / yScale);
                    cairo_fill(cr);

                    if (debug) printf("Rendering camera image.\n");
                    render_channel(channel);
                }

                jpeg_finish_decompress(&cinfo);
            }
        }

        if (src_surface!=NULL)  cairo_surface_destroy(src_surface);
        cairo_restore(cr);
        jpeg_destroy_decompress(&cinfo);

    }else {
        fprintf(stderr, ERROR_INVALID_2D_CHANNEL);
    }
}