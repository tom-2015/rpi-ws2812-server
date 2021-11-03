#include "fly_out.h"
//fly out pixels from left or right filling entire string with black or a color/brightness
//fly_out <channel>,<direction>,<delay>,<brightness>,<start>,<len>,<end_brightness>,<color>
//direction = 0/1 fly out from left or right default 1
//delay = delay in ms between moving pixel, default 10ms
//brightness = the final brightness of the leds that fly in
//start  = where to start effect default 0
//len = number of leds from start default length of strip
//end_brightness = brightness for all leds at the end, default is 0 = black
//color = final color of the leds default is to use the current color
//first have to call "fill <channel>,<color>" to initialze a color in each led before start fly_out
void fly_out(thread_context * context, char * args) {
	int channel=0,start=0, len=0, delay=10, direction=1, brightness=255, use_color=0, end_brightness=0;
	unsigned int color, tmp_color, repl_color;
	
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)) len=get_led_count(channel);
	args = read_int(args, & direction);
	args = read_int(args, & delay);
	args = read_int(args, & brightness);
	args = read_int(args, & start);
	args = read_int(args, & len);
	args = read_int(args, & end_brightness);
	use_color = (args!=NULL && (*args)!=0);
	
	if (is_valid_channel_number(channel)){		
		args = read_color_arg(args, & color, get_color_size(channel));
        if (start<0) start=0;
        if (start+len> get_led_count(channel)) len = get_led_count(channel)-start;
        
        if (debug) printf("fly_out %d,%d,%d,%d,%d,%d,%d,%d,%d\n", channel, direction, delay, brightness, start, len, end_brightness, color, use_color);
        
        int numPixels = len; //get_led_count(channel);;
        int i, j;
        ws2811_led_t * leds = get_led_string(channel);
		
		render_channel(channel);
		for (i=0;i<len;i++){
			if (direction){
				repl_color = leds[start+i].color;
			}else{
				repl_color = leds[start+len-i-1].color;
			}			
			if (direction){				
				leds[start+i].brightness = end_brightness;
				if (use_color) leds[start+i].color = color;
			}else{
				leds[start+len-i-1].brightness = end_brightness;
				if (use_color) leds[start+len-i-1].color = color;				
			}
			
			for (j=0;j<=i;j++){
				if (direction){
					leds[start+i-j].brightness = brightness;
					tmp_color = leds[start+i-j].color;
					leds[start+i-j].color = repl_color;
				}else{
					leds[start+len-i-1+j].brightness = brightness;
					tmp_color = leds[start+len-i-1+j].color;
					leds[start+len-i-1+j].color = repl_color;
				}
				render_channel(channel);
				usleep(delay * 1000);
				if (direction){
					leds[start+i-j].brightness = end_brightness;	
					leds[start+i-j].color = tmp_color;
				}else{
					leds[start+len-i-1+j].brightness = end_brightness;
					leds[start+len-i-1+j].color = tmp_color;
				}
				if (context->end_current_command) break; //signal to exit this command
			}
			
			if (context->end_current_command) break; //signal to exit this command
			render_channel(channel);
			usleep(delay * 1000);
						
		}

    }else{
        fprintf(stderr,ERROR_INVALID_CHANNEL);
    }
}