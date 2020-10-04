//fly in pixels from left or right filling entire string with a color
//fly_in <channel>,<direction>,<delay>,<brightness>,<start>,<len>,<start_brightness>,<color>
//direction = 0/1 fly in from left or right default 1
//delay = delay in ms between moving pixel, default 10ms
//brightness = the final brightness of the leds that fly in
//start  = where to start effect default 0
//len = number of leds from start default length of strip
//start_brightness = initial brightness for all leds default is 0 (black)
//color = final color of the leds default is to use the current color
//first have to call "fill <channel>,<color>" to initialze a color if you leave color default value
void fly_in(thread_context * context, char * args) {
	int channel=0,start=0, len=0, brightness=255, delay=10, direction=1, start_brightness=0, use_color=0;
	unsigned int color, tmp_color, repl_color;
	
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)) len=ledstring.channel[channel].count;
	args = read_int(args, & direction);
	args = read_int(args, & delay);
	args = read_int(args, & brightness);
	args = read_int(args, & start);
	args = read_int(args, & len);
	args = read_int(args, & start_brightness);
	use_color = (args!=NULL && (*args)!=0);

	if (is_valid_channel_number(channel)){
		args = read_color_arg(args, & color, ledstring.channel[channel].color_size);		
        if (start<0) start=0;
        if (start+len> ledstring.channel[channel].count) len = ledstring.channel[channel].count-start;
        
        if (debug) printf("fly_in %d,%d,%d,%d,%d,%d,%d,%d,%d\n", channel, direction, delay, brightness, start, len, start_brightness, color, use_color);
        
        int numPixels = len; //ledstring.channel[channel].count;;
        int i, j;
        ws2811_led_t * leds = ledstring.channel[channel].leds;
		
		for (i=0;i<len;i++){
			leds[start+i].brightness=start_brightness;
		}
		
		ws2811_render(&ledstring);
		for (i=0;i<len;i++){
			if (use_color){
				repl_color = color;
			}else{
				if (direction){
					repl_color = leds[start+len-i-1].color;
				}else{
					repl_color = leds[start+i].color;
				}
			}
			for (j=0;j<len - i;j++){
				if (direction){
					leds[start+j].brightness = brightness;
					tmp_color = leds[start+j].color;
					leds[start+j].color = repl_color;
				}else{
					leds[start+len-j-1].brightness = brightness;
					tmp_color = leds[start+len-j-1].color;
					leds[start+len-j-1].color = repl_color;
				}
				ws2811_render(&ledstring);
				usleep(delay * 1000);
				if (direction){
					leds[start+j].brightness = start_brightness;	
					leds[start+j].color = tmp_color;
				}else{
					leds[start+len-j-1].brightness = start_brightness;
					leds[start+len-j-1].color = tmp_color;
				}
				if (context->end_current_command) break; //signal to exit this command
			}
			if (direction){
				leds[start+len-i-1].brightness = brightness;
				leds[start+len-i-1].color = repl_color;
			}else{
				leds[start+i].brightness = brightness;
				leds[start+i].color = repl_color;				
			}
			ws2811_render(&ledstring);
			usleep(delay * 1000);		
			if (context->end_current_command) break; //signal to exit this command
		}

    }else{
        fprintf(stderr,ERROR_INVALID_CHANNEL);
    }
}