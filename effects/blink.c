//makes some leds blink between 2 given colors for x times with a given delay
//blink <channel>,<color1>,<color2>,<delay>,<blink_count>,<startled>,<len>
void blink (thread_context * context, char * args){
	int channel=0, color1=0, color2=0xFFFFFF,delay=1000, count=10;
	unsigned int start=0, len=0;
    
    if (is_valid_channel_number(channel)){
        len = ledstring.channel[channel].count;;
    }
	
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)){
		len = ledstring.channel[channel].count;;
	}	
	
	if (is_valid_channel_number(channel)) args = read_color_arg(args, & color1, ledstring.channel[channel].color_size);
	if (is_valid_channel_number(channel)) args = read_color_arg(args, & color2, ledstring.channel[channel].color_size);
	args = read_int(args, & delay);
	args = read_int(args, & count);
	args = read_int(args, & start);
	args = read_int(args, & len);
	
            
	if (is_valid_channel_number(channel)){

        if (start>=ledstring.channel[channel].count) start=0;
        if ((start+len)>ledstring.channel[channel].count) len=ledstring.channel[channel].count-start;
        
        if (delay<=0) delay=100;
        
        if (debug) printf("blink %d, %d, %d, %d, %d, %d, %d\n", channel, color1, color2, delay, count, start, len);
        
        ws2811_led_t * leds = ledstring.channel[channel].leds;
        int i,blinks;
        for (blinks=0; blinks<count;blinks++){
            for (i=start;i<start+len;i++){
                if ((blinks%2)==0) {
					leds[i].color=color1;
				}else{
					leds[i].color=color2;
				}
            }
            ws2811_render(&ledstring);
            usleep(delay * 1000);
			if (context->end_current_command) break; //signal to exit this command
        } 
    }else{
        fprintf(stderr,ERROR_INVALID_CHANNEL);
    }
}
