#include "chaser.h"
//chaser makes leds run accross the led strip
//chaser <channel>,<duration>,<color>,<count>,<direction>,<delay>,<start>,<len>,<brightness>,<loops>
//channel = 1
//duration = time in seconds, or 0 4ever
//color = color to use
//count = number of leds
//direction = scroll direction
//delay = delay between moving the leds, speed
//start = start index led (default 0)
//len = length of the chaser (default enitre strip)
//brightness = brightness of the chasing leds
//loops = max number of chasing loops, 0 = 4ever, default = 0
void chaser(thread_context * context, char * args){
	unsigned int channel=0, direction=1, duration=10, delay=10, color=255, brightness=255, loops=0;
	int i, n, index, len=0, count=1, start=0;
	
	args = read_channel(args, & channel);

	if (is_valid_channel_number(channel)){
		len = get_led_count(channel);
		args = read_int(args, & duration);
		args = read_color_arg(args, & color, get_color_size(channel));
		args = read_int(args, & count);
		args = read_int(args, & direction);
		args = read_int(args, & delay);
		args = read_int(args, & start);
		args = read_int(args, & len);
		args = read_brightness(args, & brightness);
		args = read_int(args, & loops);
	}
	
	if (is_valid_channel_number(channel)){
		if (start>=get_led_count(channel)) start=0;
		if ((start+len)>get_led_count(channel)) len=get_led_count(channel)-start;
		if (len==0) len = 1;
		if (count>len) count = len;
		
		if (debug) printf("chaser %d %d %d %d %d %d %d %d %d %d\n", channel, duration, color, count, direction, delay, start, len, brightness, loops);
	
		ws2811_led_t * org_leds = malloc(len * sizeof(ws2811_led_t));
		ws2811_led_t * leds = get_led_string(channel);
		memcpy(org_leds, &leds[start], len * sizeof(ws2811_led_t)); //create a backup of original leds
		
		int loop_count=0;
		
		unsigned int start_time = time(0);
		while (((((time(0) - start_time) < duration) || duration==0) && (loops==0 || loops < loop_count)) && context->end_current_command==0){
			ws2811_led_t tmp_led;
			
			for (n=0;n<count;n++){
				index = direction==1 ? i - n: len - i + n;
				if (loop_count>0 || (index > 0 && index < len)){
					index = (index + len) % len;
					leds[start + index].color = color;
					leds[start + index].brightness = brightness;	
				}
			}
			
			render_channel(channel);
			usleep(delay * 1000);
			
			for (n=0;n<count;n++){
				index = direction==1 ? i - n : len - i + n;
				index = (index + len) % len;			
				leds[start + index].color = org_leds[index].color;
				leds[start + index].brightness = org_leds[index].brightness;	
			}
			
			i++;
			i = i % len;
			if (i==0){
				loop_count++;
			}
		}	
	
		memcpy(org_leds, & leds[start], len * sizeof(ws2811_led_t));
		free(org_leds);
	}else{
		fprintf(stderr, ERROR_INVALID_CHANNEL);
	}
}