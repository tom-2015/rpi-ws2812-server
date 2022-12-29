#include <ctype.h>
#include "image.h"
#include "../gifdec.h"
#include "../jpghelper.h"
cairo_surface_t* cairo_image_surface_create_from_jpg(const char* filename);


//draw a png file to surface
//draw_image <channel>,<file_name>,<dst_x>,<dst_y>,<src_x>,<src_y>,<dst_width>,<dst_height>,<src_width>,<src_height>,<speed>,<max_loops>,<gif_trans>
void draw_image(thread_context* context, char* args) {
    char filename[MAX_VAL_LEN] = { 0 };
    char fileext[MAX_VAL_LEN] = { 0 };
    int channel = 0, max_loops = 0, src_x = 0, src_y = 0, src_width = 0, src_height = 0, i;
    double dst_x = 0.0, dst_y = 0.0, dst_width = 0.0, dst_height = 0.0;
	int gif_trans = 1;
    double speed = 1.0;
    bool is_gif = false;
    gd_GIF* gif = NULL;
    unsigned char* gif_buffer = NULL;

    cairo_surface_t* src_surface;

    args = read_channel(args, &channel);

    if (is_valid_2D_channel_number(channel)) {
        args = read_str(args, filename, MAX_VAL_LEN);

        char* c = &filename[strlen(filename) - 1];
        while (c != &filename[0] && *c!=0 && *c!='.') {
            c--;
        }

        strcpy(fileext, c);
        for (i = 0;fileext[i];i++) fileext[i] = tolower(fileext[i]);
            
        if (strcmp(fileext, ".png") == 0) {
            src_surface = cairo_image_surface_create_from_png(filename);
        }else if (strcmp(fileext, ".jpg") == 0 || strcmp(fileext, ".jpeg") == 0) {
            src_surface = cairo_image_surface_create_from_jpg(filename);
        }else if (strcmp(fileext, ".gif")==0){
            gif = gd_open_gif(filename);
            if (gif == NULL) {
                fprintf(stderr, "Error reading GIF %s.\n", filename);
                return;
            }
            dst_width = gif->width;
            dst_height = gif->height;
            gif_buffer = (unsigned char *) malloc(gif->width * gif->height * 3);
            src_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, gif->width, gif->height);
            is_gif = true;
        } else {
            printf("Error draw_image unsupported image type %s\n", fileext);
            return;
        }

        if (src_surface == NULL) {
            fprintf(stderr, "Error reading %s.\n", filename);
            return;
        }

        if (!is_gif) {
            dst_width = cairo_image_surface_get_width(src_surface);
            dst_height = cairo_image_surface_get_height(src_surface);
        }

        src_width = dst_width;
        src_height = dst_height;

        args = read_double(args, &dst_x);
        args = read_double(args, &dst_y);
        args = read_int(args, &src_x);
        args = read_int(args, &src_y);
        args = read_double(args, &dst_width);
        args = read_double(args, &dst_height);
        args = read_int(args, &src_width);
        args = read_int(args, &src_height);
        args = read_double(args, &speed);
        args = read_int(args, &max_loops);
        args = read_int(args, &gif_trans);

        if (debug) printf("draw_image %d,%s,%s,%f,%f,%d,%d,%f,%f,%d,%d,%f,%d,%d\n", channel, filename, fileext, dst_x, dst_y, src_x, src_y, dst_width, dst_height, src_width, src_height, speed, max_loops, gif_trans);


        double xScale = dst_width / (float)src_width;
        double yScale = dst_height / (float)src_height;
        
        channel_info * led_channel = get_channel(channel);
        cairo_t* cr = led_channel->cr;

        cairo_save(cr);
        cairo_scale(cr, xScale, yScale);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

        if (is_gif) {
            unsigned int* pixels = (unsigned int*)cairo_image_surface_get_data(src_surface); //get pointer to pixel data
            int cairo_stride = cairo_image_surface_get_stride(src_surface)/sizeof(unsigned int); //number of pixels in 1 row of cairo pointer
            
            if (pixels == NULL) {
                fprintf(stderr, "Error getting pixel data\n");
                return;
            }
            
            if (debug) printf("GIF loop count: %d\n", gif->loop_count);

            unsigned int i = 0;

            while (( i < gif->loop_count || gif->loop_count==0) && (i < max_loops || max_loops == 0) && !context->end_current_command) {
                while (gd_get_frame(gif)) {
                    gd_render_frame(gif, gif_buffer);
                    unsigned char* color = gif_buffer;
                    for (unsigned int y = 0; y < gif->height; y++) {
                        for (unsigned int x = 0; x < gif->width; x++) {
                            if ((gif_trans!=0) && (gd_is_bgcolor(gif, color))) {
                                pixels[y * cairo_stride + x] = 0; //full transparent
                            }else{
                                pixels[y * cairo_stride + x] =  convert_cairo_color((*((unsigned int*)color)) & 0xFFFFFF);
                            }
                            color += 3;
                        }
                    }
                    cairo_surface_mark_dirty_rectangle(src_surface, 0, 0, gif->width, gif->height);

                    cairo_set_source_surface(cr, src_surface, (dst_x / xScale - src_x), (dst_y / yScale - src_y));
                   
                    cairo_rectangle(cr, dst_x / xScale, dst_y / yScale, dst_width / xScale, dst_height / yScale);
                    cairo_fill(cr);
                    render_channel(channel);
                    usleep((int) (((double) (gif->gce.delay)) * 10000.0 / speed));
                }
                i++;
                if (debug) printf("rewind GIF\n");
                gd_rewind(gif);
            }
            free(gif_buffer);
            gd_close_gif(gif);
        }else{
            cairo_set_source_surface(cr, src_surface, (dst_x / xScale - src_x), (dst_y / yScale - src_y));
            cairo_rectangle(cr, dst_x / xScale, dst_y / yScale, dst_width / xScale, dst_height / yScale);
            cairo_fill(cr);    
        }

        cairo_surface_destroy(src_surface);
        cairo_restore(cr);

    }
    else {
        fprintf(stderr, ERROR_INVALID_2D_CHANNEL);
    }
}

cairo_surface_t * cairo_image_surface_create_from_jpg(const char * filename) {
    struct jpeg_decompress_struct cinfo;
    struct my_error_mgr jerr;
	cairo_surface_t* surface;

	FILE* infile;		/* source file */
	int jpg_row_stride;		/* physical row width in output buffer */

	if ((infile = fopen(filename, "rb")) == NULL) {
		fprintf(stderr, "Error: can't open %s\n", filename);
		return NULL;
	}

	// We set up the normal JPEG error routines, then override error_exit.
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	// Establish the setjmp return context for my_error_exit to use.
	if (setjmp(jerr.setjmp_buffer)) {
		/* If we get here, the JPEG code has signaled an error.
		 * We need to clean up the JPEG object, close the input file, and return.
		 */
		jpeg_destroy_decompress(&cinfo);
		fclose(infile);
		return NULL;
	}

	// Now we can initialize the JPEG decompression object.
	jpeg_create_decompress(&cinfo);
	jpeg_stdio_src(&cinfo, infile);

	jpeg_read_header(&cinfo, TRUE);
	jpeg_start_decompress(&cinfo);

	const int cairo_pixel_size = 4; //number of bytes for each cairo pixel
    surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, cinfo.image_width, cinfo.image_height);
	unsigned char * surface_pixels = cairo_image_surface_get_data(surface); //get pointer to pixel data in cairo
	int cairo_stride = cairo_image_surface_get_stride(surface); //number of bytes in 1 row of cairo pointer

	jpg_row_stride = cinfo.output_width * cinfo.output_components; //number of bytes in 1 row of the jpg image
	JSAMPARRAY buffer;	// Output row buffer
	buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE, jpg_row_stride, 1); //pointer to pixel data from jpg

	int i;
    int row_index = 0;
	while (cinfo.output_scanline < cinfo.output_height) {
		jpeg_read_scanlines(&cinfo, buffer, 1);
		for (i = 0;i < cinfo.image_width;i++) {
				unsigned char r, g, b;
				r = buffer[0][i * cinfo.output_components];
				g = buffer[0][i * cinfo.output_components + 1];
				b = buffer[0][i * cinfo.output_components + 2];
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

    cairo_surface_mark_dirty_rectangle(surface, 0, 0, cinfo.image_width, cinfo.image_height);
	
    jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	fclose(infile);

    return surface;
}
