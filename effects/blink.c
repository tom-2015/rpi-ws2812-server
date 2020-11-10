//makes some leds blink between 2 given colors for x times with a given delay
//blink <channel>,<color1>,<color2>,<delay>,<blink_count>,<startled>,<len>
void blink (thread_context * context, char * args){
	int channel=0, color1=0, color2=0xFFFFFF,delay=1000, count=10;
	unsigned int start=0, len=0;
    
    if (is_valid_channel_number(channel)){
        len = get_led_count(channel);
    }
	
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)){
		len = get_led_count(channel);
	}	
	
	if (is_valid_channel_number(channel)) args = read_color_arg(args, & color1, get_color_size(channel));
	if (is_valid_channel_number(channel)) args = read_color_arg(args, & color2, get_color_size(channel));
	args = read_int(args, & delay);
	args = read_int(args, & count);
	args = read_int(args, & start);
	args = read_int(args, & len);
	
            
	if (is_valid_channel_number(channel)){

        if (start>=get_led_count(channel)) start=0;
        if ((start+len)>get_led_count(channel)) len=get_led_count(channel)-start;
        
        if (delay<=0) delay=100;
        
        if (debug) printf("blink %d, %d, %d, %d, %d, %d, %d\n", channel, color1, color2, delay, count, start, len);
        
        ws2811_led_t * leds = get_led_string(channel);
        int i,blinks;
        for (blinks=0; blinks<count;blinks++){
            for (i=start;i<start+len;i++){
                if ((blinks%2)==0) {
					leds[i].color=color1;
				}else{
					leds[i].color=color2;
				}
            }
            render_channel(channel);
            usleep(delay * 1000);
			if (context->end_current_command) break; //signal to exit this command
        } 
    }else{
        fprintf(stderr,ERROR_INVALID_CHANNEL);
    }
}
