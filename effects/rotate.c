void rotate_strip(thread_context * context, int channel, int nplaces, int direction, unsigned int new_color, int use_new_color, int new_brightness){
	ws2811_led_t tmp_led;
    ws2811_led_t * leds = ledstring.channel[channel].leds;
    unsigned int led_count = ledstring.channel[channel].count;
	unsigned int n,i;
	for(n=0;n<nplaces;n++){
		if (direction==1){
			tmp_led = leds[0];
			for(i=1;i<led_count;i++){
				leds[i-1] = leds[i]; 
			}
			if (use_new_color){
				leds[led_count-1].color=new_color;
				leds[led_count-1].brightness=new_brightness;
			}else{
				leds[led_count-1]=tmp_led;
			}
		}else{
			tmp_led = leds[led_count-1];
			for(i=led_count-1;i>0;i--){
				leds[i] = leds[i-1]; 
			}
			if (use_new_color){
				leds[0].color=new_color;	
				leds[0].brightness=new_brightness;
			}else{
				leds[0]=tmp_led;		
			}
		}
	}	
}

//shifts all colors 1 position
//rotate <channel>,<places>,<direction>,<new_color>,<new_brightness>
//if new color is set then the last led will have this color instead of the color of the first led
void rotate(thread_context * context, char * args){
	int channel=0, nplaces=1, direction=1;
    unsigned int new_color=0, new_brightness=255;
	int use_new_color=0;
    
	args = read_channel(args, & channel);
	args = read_int(args, & nplaces);
	args = read_int(args, & direction);
	if (is_valid_channel_number(channel)){
		use_new_color= (args!=NULL && *args!=0);
		args = read_color_arg(args, & new_color, ledstring.channel[channel].color_size);
		read_brightness(args, & new_brightness);
	}
	
	if (debug) printf("Rotate %d %d %d %d %d\n", channel, nplaces, direction, new_color, new_brightness);
	
    if (is_valid_channel_number(channel)){
		rotate_strip(context, channel, nplaces, direction, new_color, use_new_color, new_brightness);
    }else{
        fprintf(stderr,ERROR_INVALID_CHANNEL);
    }
}