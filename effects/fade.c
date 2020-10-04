//causes a fade effect in time
//fade <channel>,<startbrightness>,<endbrightness>,<delay>,<step>,<startled>,<len>
void fade (thread_context * context, char * args){
	int channel=0, brightness=255,step=1,startbrightness=0, endbrightness=255;
	unsigned int start=0, len=0, delay=50;
    
    if (is_valid_channel_number(channel)){
        len = ledstring.channel[channel].count;;
    }
	
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)){
        len = ledstring.channel[channel].count;;
    }
	args = read_int(args, & startbrightness);
	args = read_int(args, & endbrightness);
	args = read_int(args, & delay);
	args = read_int(args, & step);
	args = read_int(args, & start);
	args = read_int(args, & len);
	
            
	if (is_valid_channel_number(channel)){
        if (startbrightness>0xFF) startbrightness=255;
        if (endbrightness>0xFF) endbrightness=255;
        
        if (start>=ledstring.channel[channel].count) start=0;
        if ((start+len)>ledstring.channel[channel].count) len=ledstring.channel[channel].count-start;
        
        if (step==0) step = 1;
        if (startbrightness>endbrightness){ //swap
            if (step > 0) step = -step;
        }else{
            if (step < 0) step = -step;
        }        
        
        if (debug) printf("fade %d, %d, %d, %d, %d, %d, %d\n", channel, startbrightness, endbrightness, delay, step,start,len);
        
        ws2811_led_t * leds = ledstring.channel[channel].leds;
        int i,brightness;
        for (brightness=startbrightness; (startbrightness > endbrightness ? brightness>=endbrightness:  brightness<=endbrightness) ;brightness+=step){
            for (i=start;i<start+len;i++){
                leds[i].brightness=brightness;
            }
            ws2811_render(&ledstring);
            usleep(delay * 1000);
			if (context->end_current_command) break; //signal to exit this command
        } 
    }else{
        fprintf(stderr,ERROR_INVALID_CHANNEL);
    }
}