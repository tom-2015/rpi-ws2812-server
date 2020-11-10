//generates a brightness gradient pattern of a color component or brightness level
//gradient <channel>,<RGBWL>,<startlevel>,<endlevel>,<startled>,<len>
void gradient (thread_context * context, char * args){
    char value[MAX_VAL_LEN];
	int channel=0, startlevel=0,endlevel=255;
	unsigned int start=0, len=0;
    char component='L'; //L is brightness level
    
    if (is_valid_channel_number(channel)){
        len = get_led_count(channel);
    }
	
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)){
		len = get_led_count(channel);
	}	
	args = read_val(args, value, MAX_VAL_LEN);
	component=toupper(value[0]);
	args = read_int(args, & startlevel);
	args = read_int(args, & endlevel);
	args = read_int(args, & start);
	args = read_int(args, & len);

            
	if (is_valid_channel_number(channel)){
        if (startlevel>0xFF) startlevel=255;
        if (endlevel>0xFF) endlevel=255;
        
        if (start>=get_led_count(channel)) start=0;
        if ((start+len)>get_led_count(channel)) len=get_led_count(channel)-start;
        
        
        float step = 1.0*(endlevel-startlevel) / (float)(len-1);
        
        if (debug) printf("gradient %d, %c, %d, %d, %d,%d\n", channel, component, startlevel, endlevel, start,len);
        
        ws2811_led_t * leds = get_led_string(channel);
        
        float flevel = startlevel;
        int i;
        for (i=0; i<len;i++){
            unsigned int level = (unsigned int) flevel;
            if (i==len-1) level = endlevel;
            switch (component){
                case 'R':
                    leds[i+start].color = (leds[i+start].color & 0xFFFFFF00) | level;
                    break;
                case 'G':
                    leds[i+start].color = (leds[i+start].color & 0xFFFF00FF) | (level << 8);
                    break;
                case 'B':
                    leds[i+start].color = (leds[i+start].color & 0xFF00FFFF) | (level << 16);
                    break;
                case 'W':
                    leds[i+start].color = (leds[i+start].color & 0x00FFFFFF) | (level << 24);
                    break;
                case 'L':
                    leds[i+start].brightness=level;
                    break;
            }
            flevel+=step;
        } 
    }else{
        fprintf(stderr,ERROR_INVALID_CHANNEL);
    }
}