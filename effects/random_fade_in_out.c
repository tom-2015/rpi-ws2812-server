typedef struct {
	int led_index;
	int brightness;
	int delay; //random delay to delay effect
	int start_brightness;
	int start_color;
}fade_in_out_led_status;

//finds a random index which is currently not used by random_fade_in_out
int find_random_free_led_index(fade_in_out_led_status * led_status, unsigned int count, unsigned int start, unsigned int len){
	int i,j,k;
	int index=-1;
	int found = 1;
	k=0;
	//first try random
	while (found && k < (len * 2)){ //k to prevent endless loop
		index = start + (rand() % len);
		found = 0;
		for (j=0; j<count;j++) {
			if (led_status[j].led_index == index){
				found = 1;
				break;
			}
		}
		k++;
	}
	if (found){
		index = -1;
		//if count is too high just search for one index still available
		for (j=start;j<start+len;j++){
			found=0;
			for (i=0; i<count;i++) {
				if (led_status[i].led_index == j){
					found = 1;//found it
					break;
				}
			}
			if (found==0){ //didn't find, can use this index
				index=j;	
				break;
			}
		}
	}
	return index;
}

//creates some kind of random blinking leds effect
//random_fade_in_out <channel>,<duration Sec>,<count>,<delay>,<step>,<sync_delay>,<inc_dec>,<brightness>,<start>,<len>,<color>
//duration = total max duration of effect
//count = max number of leds that will fade in or out at same time
//delay = delay between changes in brightness
//step = ammount of brightness to increase between delays
//inc_dec = if 1 brightness will start at <brightness> and decrease to initial brightness of the led, else it will start low and go up
//start  = start at led position
//len  = stop at led position
//color  = use specific color, after blink effect color will return to initial
//brightness = max brightness of blinking led
void random_fade_in_out(thread_context * context, char * args){
	unsigned int channel=0, start=0, len=0, count=0, duration=10, delay=1, step=20, sync_delay=0, inc_dec=1, brightness=255,color=0, change_color=0, i;
    fade_in_out_led_status *led_status;
	
    if (is_valid_channel_number(channel)){
        len = get_led_count(channel);;
    }
	
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)){
		len = get_led_count(channel);
		count = len / 3;
		args = read_int(args, & duration);
		args = read_int(args, & count);
		args = read_int(args, & delay);		
		args = read_int(args, & step);
		args = read_int(args, & sync_delay);
		args = read_int(args, & inc_dec);
		args = read_int(args, & brightness);
		args = read_int(args, & start);
		args = read_int(args, & len);
		change_color = args!=NULL && *args!=0;
		args = read_color_arg(args, & color, get_color_size(channel));
		args = read_brightness(args, & brightness);
		
		if (start>=get_led_count(channel)) start=0;
        if ((start+len)>get_led_count(channel)) len=get_led_count(channel)-start;
		if (count>len) count = len;
		
		if (debug) printf("random_fade_in_out %d, %d, %d, %d, %d, %d, %d, %d, %d, %d\n", channel, count, delay, step, sync_delay, inc_dec, brightness, start, len, color);
		
		led_status = (fade_in_out_led_status *)malloc(count * sizeof(fade_in_out_led_status));
		ws2811_led_t * leds = get_led_string(channel);
		
		render_channel(channel);
		
		for (i=0; i<count;i++){ //first assign count random leds for fading
			int index=find_random_free_led_index(led_status, count, start, len);
			led_status[i].led_index = index;
			if (index!=-1){ //assign
				led_status[i].delay = sync_delay ?  (rand() % sync_delay) : 0;
				led_status[i].start_brightness = leds[index].brightness;
				led_status[i].start_color = leds[index].color;
				led_status[i].led_index = index;
				led_status[i].brightness = brightness;
			}
		}
		
		unsigned int start_time = time(0);
		
		while ((((time(0) - start_time) < duration) || duration==0) && context->end_current_command==0){
			for (i=0;i<count; i++){
				if (led_status[i].delay<=0){
					if (led_status[i].led_index!=-1){
						leds[led_status[i].led_index].brightness = led_status[i].brightness;
						if (change_color) leds[led_status[i].led_index].color = color;
						if (inc_dec) led_status[i].brightness--;
						if ((inc_dec==1 && led_status[i].brightness <= led_status[i].start_brightness) || (inc_dec==0 && led_status[i].brightness >= led_status[i].start_brightness)){
							leds[led_status[i].led_index].brightness = led_status[i].start_brightness;
							if (change_color) leds[led_status[i].led_index].color = led_status[i].start_color;
							int index=find_random_free_led_index(led_status, count, start, len);
							if (index!=-1){	
								led_status[i].led_index = index;
								led_status[i].brightness = brightness;
								led_status[i].start_brightness = leds[led_status[i].led_index].brightness;
								led_status[i].start_color = leds[led_status[i].led_index].color;
								led_status[i].delay = sync_delay ?  (rand() % sync_delay) : 0;
							}
						}
					}
				}else{
					led_status[i].delay--;
				}
			}		
			render_channel(channel);
			usleep(delay * 1000);				
		}
		
		for (i=0;i<count;i++){
			leds[led_status[i].led_index].brightness = led_status[i].start_brightness;
			if (change_color) leds[led_status[i].led_index].color = led_status[i].start_color;
		}
		render_channel(channel);
		free (led_status);
	}else{
		fprintf(stderr, ERROR_INVALID_CHANNEL);
		
	}
	
}