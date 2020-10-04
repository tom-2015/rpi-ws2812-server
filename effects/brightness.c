//dims leds
//brightness <channel>,<brightness>,<start>,<len> (brightness: 0-255)
void brightness(thread_context * context, char * args){
	int channel=0, brightness=255;
	unsigned int start=0, len=0;
    if (is_valid_channel_number(channel)){
        len = ledstring.channel[channel].count;;
    }
	
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)) len = ledstring.channel[channel].count;;
	args = read_int(args, & brightness);
	args = read_int(args, & start);
	args = read_int(args, & len);
	
	if (is_valid_channel_number(channel)){
        if (brightness<0 || brightness>0xFF) brightness=255;
        
        if (start>=ledstring.channel[channel].count) start=0;
        if ((start+len)>ledstring.channel[channel].count) len=ledstring.channel[channel].count-start;
        
        if (debug) printf("Changing brightness %d, %d, %d, %d\n", channel, brightness, start, len);
        
        ws2811_led_t * leds = ledstring.channel[channel].leds;
        unsigned int i;
        for (i=start;i<start+len;i++){
            leds[i].brightness=brightness;
        }
    }else{
        fprintf(stderr,ERROR_INVALID_CHANNEL);
    }
}