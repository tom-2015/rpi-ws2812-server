//fills pixels with rainbow effect
//count tells how many rainbows you want
//rainbow <channel>,<count>,<startcolor>,<stopcolor>,<start>,<len>
//start and stop = color values on color wheel (0-255)
void rainbow(thread_context * context, char * args) {
	int channel=0, count=1,start=0,stop=255,startled=0, len=0;
	
    if (is_valid_channel_number(channel)) len=ledstring.channel[channel].count;
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)) len=ledstring.channel[channel].count;
	args = read_int(args, & count);
	args = read_int(args, & start);
	args = read_int(args, & stop);
	args = read_int(args, & startled);
	args = read_int(args, & len);
	
	if (is_valid_channel_number(channel)){
        if (start<0 || start > 255) start=0;
        if (stop<0 || stop > 255) stop = 255;
        if (startled<0) startled=0;
        if (startled+len> ledstring.channel[channel].count) len = ledstring.channel[channel].count-startled;
        
        if (debug) printf("Rainbow %d,%d,%d,%d,%d,%d\n", channel, count,start,stop,startled,len);
        
        int numPixels = len; //ledstring.channel[channel].count;;
        int i, j;
        ws2811_led_t * leds = ledstring.channel[channel].leds;
        for(i=0; i<numPixels; i++) {
            leds[startled+i].color = deg2color(abs(stop-start) * i * count / numPixels + start);
        }
    }else{
        fprintf(stderr,ERROR_INVALID_CHANNEL);
    }
}