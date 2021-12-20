#include "read_png.h"
#include "../readpng.h"
//read PNG image and put pixel data to LEDS
//readpng <channel>,<FILE>,<BACKCOLOR>,<start>,<len>,<offset>,<OR AND XOR =>,<DELAY>,<flip even rows>
//offset = where to start in PNG file
//backcolor = color to use for transparent area, FF0000 = RED
//P = use the PNG backcolor (default)
//W = use the alpha data for the White leds in RGBW LED strips
//DELAY = delay ms between 2 reads of LEN pixels, default=0 if 0 only <len> bytes at <offset> will be read
void readpng(thread_context * context,char * args){
	//struct jpeg_decompress_struct cinfo;
	//struct my_error_mgr jerr;
	
	char value[MAX_VAL_LEN];
	int channel=0;
	char filename[MAX_VAL_LEN];
	unsigned int start=0, len=0, offset=0,flip_rows=0;
	int op=0;
	int backcolor=0;
    int backcolortype=0; //0 = use PNG backcolor, 1 = use given backcolor, 2 = no backcolor but use alpha for white leds
	int delay=0;
	int row_index=0;
	
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)) len = get_led_count(channel);
	args = read_str(args, filename, sizeof(filename));
	args = read_str(args, value, sizeof(filename));
	if (strlen(value)>=6){
		if (is_valid_channel_number(channel)){
			read_color(value, & backcolor, get_color_size(channel));
			backcolortype=1;
		}
	}else if (strcmp(value, "W")==0){
		backcolortype=2;
	}	
	args = read_int(args, &start);
	args = read_int(args, &len);
	args = read_int(args, &offset);
	args = read_str(args, value, sizeof(value));
	if (strcmp(value, "OR")==0) op=1;
	else if (strcmp(value, "AND")==0) op=2;
	else if (strcmp(value, "XOR")==0) op=3;
	else if (strcmp(value, "NOT")==0) op=4;
	args = read_int(args, &delay);
	args = read_int(args, &flip_rows);
	
	if (is_valid_channel_number(channel)){
		FILE * infile;		/* source file */
		png_object png;
		int rc;
		uch bg_red=0, bg_green=0, bg_blue=0;

		if (start<0) start=0;
        if (start+len> get_led_count(channel)) len = get_led_count(channel) -start;
		int color_size = get_color_size(channel);

		if (debug) printf("readpng %d,%s,%d,%d,%d,%d,%d,%d\n", channel, filename, backcolor, start, len,offset,op, delay);
		
		if ((infile = fopen(filename, "rb")) == NULL) {
			fprintf(stderr, "Error: can't open %s\n", filename);
			return;
		}
		
		if ((rc = readpng_init(infile, &png)) != 0) {
            switch (rc) {
                case 1:
                    fprintf(stderr, "[%s] is not a PNG file: incorrect signature.\n", filename);
                    break;
                case 2:
                    fprintf(stderr, "[%s] has bad IHDR (libpng longjmp).\n", filename);
                    break;
                case 4:
                    fprintf(stderr, "Read PNG insufficient memory.\n");
                    break;
                default:
                    fprintf(stderr, "Unknown readpng_init() error.\n");
                    break;
            }
            fclose(infile);
			return;
        }
		
		//get the background color (for transparency support)
		if (backcolortype==0){
			bg_red = png.background_red;
			bg_green = png.background_green;
			bg_blue = png.background_blue;
		}else{
			bg_red = get_red(backcolor);
			bg_green = get_green(backcolor);
			bg_blue = get_blue(backcolor);
		}
		
		//read entire image data
		uch * image_data = readpng_get_image(& png, 2.2);
		
		if (image_data) {
			int row=0, led_idx=0, png_idx=0, i=0;
			uch r, g, b, a;
			uch *src;
			
			ws2811_led_t * leds = get_led_string(channel);
		
			if (start>=get_led_count(channel)) start=0;
			if ((start+len)> get_led_count(channel)) len= get_led_count(channel) -start;
			
			led_idx=start; //start at this led index
			//load all pixels
			for (row = 0;  row < png.height; row++) {
				src = image_data + row * png.rowbytes;
				
				for (i = png.width;  i > 0;  --i) {
					r = *src++;
					g = *src++;
					b = *src++;
					
					if (png.channels != 3){
						a = *src++;
						if (backcolortype!=2){
							r = alpha_component(r, bg_red,a);
							g = alpha_component(g, bg_green,a);
							b = alpha_component(b, bg_blue,a);
						}
					}					
					if (png_idx>=offset){
						if (debug) printf("led %d= r %d,g %d,b %d,a %d, PNG channels=%d, PNG idx=%d\n", led_idx, r, g, b, a,png.channels,png_idx);
						if (led_idx < start + len){
							int fill_color;
							if (backcolortype==2 && color_size>3){
								fill_color=color_rgbw(r,g,b,a);
							}else{
								fill_color=color(r,g,b);
							}
							
							if (flip_rows){ //this will horizontaly flip the row
								if (row_index & 1){
									led_idx = start + png.width * row_index + (png.width - i);
								}
							}
		
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
						if ( led_idx>=start + len){ 
							if (delay!=0){//reset led index if we are at end of led string and delay
								led_idx=start;
								row_index=0;
								render_channel(channel);
								usleep(delay * 1000);
							}else{
								row = png.height; //exit reading
								i=0;
								break;
							}
						}
					}
					png_idx++;
					if (context->end_current_command) break; //signal to exit this command
				}
				if (context->end_current_command) break;
				row_index++;
			}
			readpng_cleanup(&png);
		}else{
			readpng_cleanup(&png);
			fprintf(stderr, "Unable to decode PNG image\n");
		}
		fclose(infile);
    }
}