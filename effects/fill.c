#include "fill.h"
//fills leds with certain color
//fill <channel>,<color>,<start>,<len>,<OR,AND,XOR,NOT,=>
void fill(thread_context * context, char * args){
    char op=0;
	int channel=0,start=0,len=-1;
	unsigned int fill_color=0;
	
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)) args = read_color_arg(args, & fill_color, get_color_size(channel));
    args = read_int(args, & start);
	args = read_int(args, & len);
	args = read_operation(args, & op);

	
	if (is_valid_channel_number(channel)){
        if (start<0 || start>=get_led_count(channel)) start=0;        
        if (len<=0 || (start+len)>get_led_count(channel)) len=get_led_count(channel)-start;

        if (debug) printf("fill %d,%d,%d,%d,%d\n", channel, fill_color, start, len,op);
        
        ws2811_led_t * leds = get_led_string(channel);
        unsigned int i;
        for (i=start;i<start+len;i++){
            switch (op){
                case OP_EQUAL:
                    leds[i].color=fill_color;
                    break;
                case OP_OR:
                    leds[i].color|=fill_color;
                    break;
                case OP_AND:
                    leds[i].color&=fill_color;
                    break;
                case OP_XOR:
                    leds[i].color^=fill_color;
                    break;
                case OP_NOT:
                    leds[i].color=~leds[i].color;
                    break;
            }
        }
    }else{
        fprintf(stderr,ERROR_INVALID_CHANNEL);
    }
}