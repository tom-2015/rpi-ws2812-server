//fills pixels with rainbow effect
//count tells how many rainbows you want
//color_change <channel>,<startcolor>,<stopcolor>,<duration>,<start>,<len>
//start and stop = color values on color wheel (0-255)
void color_change(thread_context * context, char * args) {
	int channel=0, count=1,start=0,stop=255,startled=0, len=0, duration=10000, delay=10;
	
    if (is_valid_channel_number(channel)) len=ledstring.channel[channel].count;
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)) len=ledstring.channel[channel].count;
	args = read_int(args, & start);
	args = read_int(args, & stop);
	args = read_int(args, & duration);
	args = read_int(args, & startled);
	args = read_int(args, & len);
	
	if (is_valid_channel_number(channel)){
        if (start<0 || start > 255) start=0;
        if (stop<0 || stop > 255) stop = 255;
        if (startled<0) startled=0;
        if (startled+len> ledstring.channel[channel].count) len = ledstring.channel[channel].count-startled;
        
        if (debug) printf("color_change %d,%d,%d,%d,%d,%d\n", channel, start, stop, duration, startled, len);
        
        int numPixels = len; //ledstring.channel[channel].count;;
        int i, j;
        ws2811_led_t * leds = ledstring.channel[channel].leds;
		
		unsigned long long start_time = time_ms();
		unsigned long long curr_time = time_ms() - start_time;
		
		while (curr_time < duration){
			unsigned int color = deg2color(abs(stop-start) * curr_time / duration + start);
			
			for(i=0; i<numPixels; i++) {
				leds[startled+i].color = color;
			}			
			
			ws2811_render(&ledstring);
			usleep(delay * 1000);	
			curr_time = time_ms() - start_time;	
			if (context->end_current_command) break; //signal to exit this command			
		}
		
		

    }else{
        fprintf(stderr,ERROR_INVALID_CHANNEL);
    }
}