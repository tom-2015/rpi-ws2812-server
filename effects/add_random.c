//generates random colors
//random <channel>,<start>,<len>,<RGBWL>
void add_random(thread_context * context, char * args){
    char value[MAX_VAL_LEN];
	int channel=0;
	unsigned int start=0, len=0;
    char component='L'; //L is brightness level
    int use_r=1, use_g=1, use_b=1, use_w=1, use_l=1;
    
    if (is_valid_channel_number(channel)){
        len = ledstring.channel[channel].count;;
    }
	
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)){
		len = ledstring.channel[channel].count;;
	}	
	args = read_int(args, & start);
	args = read_int(args, & len);
	if (args!=NULL && *args!=0){
		args = read_val(args, value, MAX_VAL_LEN);
		use_r=0, use_g=0, use_b=0, use_w=0, use_l=0;
		unsigned char i;
		for (i=0;i<strlen(value);i++){
			switch(toupper(value[i])){
				case 'R':
					use_r=1;
					break;
				case 'G':
					use_g=1;
					break;
				case 'B':
					use_b=1;
					break;
				case 'W':
					use_w=1;
					break;
				case 'L':
					use_l=1;
					break;
			}
		}
	}	
    
    if (is_valid_channel_number(channel)){

        if (start>=ledstring.channel[channel].count) start=0;
        if ((start+len)>ledstring.channel[channel].count) len=ledstring.channel[channel].count-start;
     
        if (debug) printf("random %d,%d,%d\n", channel, start, len);
        
        ws2811_led_t * leds = ledstring.channel[channel].leds;
        //unsigned int colors = ledstring[channel].color_size;
        unsigned char r=0,g=0,b=0,w=0,l=0;
        unsigned int i;
        for (i=0; i<len;i++){
            if (use_r) r = rand() % 256;
            if (use_g) g = rand() % 256;
            if (use_b) b = rand() % 256;
            if (use_w) w = rand() % 256;
            if (use_l) l = rand() % 256;
            
            if (use_r || use_g || use_b || use_w) leds[start+i].color = color_rgbw(r,g,b,w);
            if (use_l) leds[start+i].brightness = l;
        }
    }else{
        fprintf(stderr,ERROR_INVALID_CHANNEL);
    }
}