#include "read_jpg.h"
#include "../jpghelper.h"
//read JPEG image and put pixel data to LEDS
//readjpg <channel>,<FILE>,<start>,<len>,<offset>,<OR AND XOR NOT =>,<delay>,<flip_rows>
//offset = where to start in JPEG file
//DELAY = delay ms between 2 reads of LEN pixels, default=0 if 0 only <len> bytes at <offset> will be read
void readjpg(thread_context * context, char * args){
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	
	char value[MAX_VAL_LEN];
	int channel=0;
	char filename[MAX_VAL_LEN];
	unsigned int start=0, len=1, offset=0, flip_rows=0, row_index=0;
	int op=0,delay=0;
    
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)) len = get_led_count(channel);
	args = read_str(args, filename, sizeof(filename));
	args = read_int(args, &start);
	args = read_int(args, &len);
	args = read_int(args, &offset);
	args = read_str(args, value, sizeof(value));
	if (strcmp(value, "OR")==0) op=1;
	else if (strcmp(value, "AND")==0) op=2;
	else if (strcmp(value, "XOR")==0) op=3;
	else if (strcmp(value, "NOT")==0) op=4;
	args = read_int(args, &delay);
	if (len<=0) len =1;
    
    if (is_valid_channel_number(channel)){
		FILE * infile;		/* source file */
		int row_stride;		/* physical row width in output buffer */
		
		
		if (debug) printf("readjpg %d,%s,%d,%d,%d,%d,%d\n", channel, filename, start, len, offset, op, delay);
		
		if ((infile = fopen(filename, "rb")) == NULL) {
			fprintf(stderr, "Error: can't open %s\n", filename);
			return;
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
			return;
		}
		
		// Now we can initialize the JPEG decompression object.
		jpeg_create_decompress(&cinfo);
		jpeg_stdio_src(&cinfo, infile);

		jpeg_read_header(&cinfo, TRUE);
		jpeg_start_decompress(&cinfo);

		row_stride = cinfo.output_width * cinfo.output_components;

		JSAMPARRAY buffer;	// Output row buffer
		int i=0,jpg_idx=0,led_idx; //pixel index for current row, jpeg image, led string
		buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

		ws2811_led_t * leds = get_led_string(channel);
		
		if (start>=get_led_count(channel)) start=0;
		if ((start+len)> get_led_count(channel)) len= get_led_count(channel)-start;
		
		led_idx=start; //start at this led index
		
		int eofstring=0;
		while (eofstring==0 && cinfo.output_scanline < cinfo.output_height && context->end_current_command==0) {
			jpeg_read_scanlines(&cinfo, buffer, 1);
			for(i=0;i<cinfo.image_width;i++){
				if (jpg_idx>=offset){ //check jpeg offset
					unsigned char r,g,b;
					r = buffer[0][i*cinfo.output_components];
					g = buffer[0][i*cinfo.output_components+1];
					b = buffer[0][i*cinfo.output_components+2];
					if (cinfo.output_components==1){ //grayscale image
						g = r;
						b = r;
					}
					if (flip_rows){ //this will horizontaly flip the row
						if (row_index & 1){
							led_idx = start + cinfo.image_width * row_index + (cinfo.image_width - i);
						}
					}
					if (led_idx<start+len){
						if (debug) printf("led %d= r %d,g %d,b %d, jpg idx=%d\n", led_idx, r, g, b,jpg_idx);
						int fill_color = color(r,g,b);
						switch (op){
							case 0:
								leds[led_idx].color=fill_color;
								break;
							case 1:
								leds[i].color|=fill_color;
								break;
							case 2:
								leds[i].color&=fill_color;
								break;
							case 3:
								leds[i].color^=fill_color;
								break;
							case 4:
								leds[i].color=~fill_color;
								break;
						}
					}
					led_idx++;
					if ( led_idx >= start + len){ 
						if (delay!=0){//reset led index if we are at end of led string and delay
							led_idx=start;
							row_index=0;
							render_channel(channel);
							usleep(delay * 1000);
						}else{
							eofstring=1;
							break;
						}
					}
				}
				if (context->end_current_command) break; //signal to exit this command
				jpg_idx++;
			}
			row_index++;
		}

		jpeg_finish_decompress(&cinfo);
		jpeg_destroy_decompress(&cinfo);
		fclose(infile);
	}
}