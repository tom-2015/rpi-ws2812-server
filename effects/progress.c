#include "progress.h"

void set_progress(ws2811_led_t * leds, int direction, int start, int len, float value, int brightness_on, int brightness_off){
	int i;
	if (value>100) value=100;
	if (direction){
		value = (float)len * value / 100.0f;
		for (i=0;i<len;i++){
			leds[start+i].brightness = (float)(i+1) <= value ? brightness_on : brightness_off; //(100 * i / len) <= * value ? brightness_on : brightness_off;
		}
	}else{
		value = 100.0f - value;
		value = (float)len * value / 100.0f;
		for (i=0;i<len;i++){
			leds[start+i].brightness = (float)i < value ? brightness_off : brightness_on; // (100 * i / len) < value ? brightness_off : brightness_on;
		}	
	}
}

//generate a progress_bar effect by changing the brightness of the leds
//first use fill or something else to set a each led the color you want
//<channel> default 1
//<direction> default 1, start 0% --> stop 100% (0 to reverse)
//<delay> delay to use between increase, default 1s, use 0 to not automatically fill progress bar
//<start> start at this led default 0
//<len> use this ammount of leds for progress bar
//<brightness_on> the brightness value for leds that are turned on, default 255
//<brightness_off> the brightness value for leds that are turned off, default 0
//<value> set progress bar to this value in % (0-100), use a delay of 0
void progress(thread_context * context, char * args) {
	int channel=0,start=0, len=0, brightness_on=255, brightness_off=0, delay=1000, direction=1;
	float value =0.0;
	unsigned int color, tmp_color, repl_color;
	
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)) len=get_led_count(channel);
	args = read_int(args, & direction);
	args = read_int(args, & delay);
	args = read_int(args, & start);
	args = read_int(args, & len);
	args = read_int(args, & brightness_on);
	args = read_int(args, & brightness_off);
	args = read_float(args, & value);

	if (is_valid_channel_number(channel)){		
        if (start<0) start=0;
        if (start+len> get_led_count(channel)) len = get_led_count(channel)-start;
        
        if (debug) printf("progress %d,%d,%d,%d,%d,%d,%d,%d,%d\n", context->id, channel, direction, delay, start, len, brightness_on, brightness_off, value);
       
        int i;
        ws2811_led_t * leds = get_led_string(channel);

		if (delay==0){
			if (value<0) value=0;
			set_progress(leds, direction, start, len, value, brightness_on, brightness_off);
		}else{
			for (i=0;i<=len;i++){
				set_progress(leds, direction, start, len, 100.0f * (float)i/(float)len, brightness_on, brightness_off);
				render_channel(channel);
				usleep(delay * 1000);		
				if (context->end_current_command) break; //signal to exit this command
			}
		}

    }else{
        fprintf(stderr,ERROR_INVALID_CHANNEL);
    }
}