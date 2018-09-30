#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <pthread.h>
#include <ctype.h>
#include "ws2811.h"

#define DEFAULT_DEVICE_FILE "/dev/ws281x"
#define DEFAULT_COMMAND_LINE_SIZE 1024
#define DEFAULT_BUFFER_SIZE 32768

#define MAX_KEY_LEN 255
#define MAX_VAL_LEN 255
#define MAX_LOOPS 32

#define MODE_STDIN 0
#define MODE_NAMED_PIPE 1
#define MODE_FILE 2
#define MODE_TCP 3

//USE_JPEG and USE_PNG is enabled through make file
//to disable compilation of PNG / or JPEG use:
//make PNG=0 JPEG=0
//#define USE_JPEG 1
//#define USE_PNG 1

#ifdef USE_JPEG
	#include "jpeglib.h"
	#include <setjmp.h>
	//from https://github.com/LuaDist/libjpeg/blob/master/example.c
	struct my_error_mgr {
	  struct jpeg_error_mgr pub;	/* "public" fields */

	  jmp_buf setjmp_buffer;	/* for return to caller */
	};

	typedef struct my_error_mgr * my_error_ptr;

	/*
	 * Here's the routine that will replace the standard error_exit method:
	 */

	METHODDEF(void)
	my_error_exit (j_common_ptr cinfo)
	{
	  /* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
	  my_error_ptr myerr = (my_error_ptr) cinfo->err;

	  /* Always display the message. */
	  /* We could postpone this until after returning, if we chose. */
	  (*cinfo->err->output_message) (cinfo);

	  /* Return control to the setjmp point */
	  longjmp(myerr->setjmp_buffer, 1);
	}
#endif

#ifdef USE_PNG
	#include "readpng.h"
#endif

//for easy and fast converting asci hex to integer
char hextable[256] = {
   [0 ... 255] = 0, // bit aligned access into this table is considerably
   ['0'] = 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, // faster for most modern processors,
   ['A'] = 10, 11, 12, 13, 14, 15,       // for the space conscious, reduce to
   ['a'] = 10, 11, 12, 13, 14, 15        // signed char.
};

/*void init_hextable(){
    unsigned int i;
    for(i=0;i<256;i++){
        if (i >= '0' && i<= '9') hextable[i]=i-'0';
        if (i >= 'a' && i<= 'f') hextable[i]=i-'a'+10;
        if (i >= 'A' && i<= 'F') hextable[i]=i-'A'+10;
    }
}*/

typedef struct {
    int do_pos;
    int n_loops;
} do_loop;


FILE *    input_file;         //the named pipe handle
char *    command_line;       //current command line
char *    named_pipe_file;    //holds named pipe file name 
int       command_index;      //current position
int       command_line_size;  //max bytes in command line
int       exit_program=0;     //set to 1 to exit the program
int       mode;               //mode we operate in (TCP, named pipe, file, stdin)
do_loop   loops[MAX_LOOPS]={0};      //positions of 'do' in file loop, max 32 recursive loops
int       loop_index=0;       //current loop index
int       debug=0;            //set to 1 to enable debug output

//for TCP mode
int sockfd;        //socket that listens
int active_socket; //current active connection socket
socklen_t clilen;
struct sockaddr_in serv_addr, cli_addr;

//for TCP/IP multithreading
char *       thread_data=NULL;         //holds command to execute in separate thread (TCP/IP only)
int          thread_read_index=0;      //read position 
int          thread_write_index=0;     //write position
int          thread_data_size=0;       //buffer size
volatile int thread_running=0;         //becomes 1 there is a thread running
int          write_to_thread_buffer=0; //becomes 1 if we need to write to thread buffer
int          start_thread=0;           //becomes 1 after the thread_stop command and tells the program to start the thread on disconnect of the TCP/IP connection
//pthread_mutex_t mutex_fifo_queue; 
pthread_t thread; //a thread that will repeat code after client closed connection

ws2811_t ledstring;

void process_character(char c);

//handles exit of program with CTRL+C
static void ctrl_c_handler(int signum){
	exit_program=1;
}

static void setup_handlers(void){
    struct sigaction sa;
    sa.sa_handler = ctrl_c_handler,
    sigaction(SIGKILL, &sa, NULL);
}

//allocates memory for command line
void malloc_command_line(int size){
	if (command_line!=NULL) free(command_line);
	command_line = (char *) malloc(size+1);
	command_line_size = size;
	command_index = 0;
}

unsigned char get_red(int color){
	return color & 0xFF;
}

unsigned char get_green(int color){
	return (color >> 8) & 0xFF;
}

unsigned char get_blue(int color){
	return (color >> 16) & 0xFF;
}

unsigned char get_white(int color){
	return (color >> 24) & 0xFF;
}

//returns a color from RGB value
//note that the ws281x stores colors as GRB
int color (unsigned char r, unsigned char g, unsigned char b){
	return (b << 16) + (g << 8) + r;
}

//returns new colorcomponent based on alpha number for component1 and background component
//all values are unsigned char 0-255
unsigned char alpha_component(unsigned int component, unsigned int bgcomponent, unsigned int alpha){
	return component * alpha / 255 + bgcomponent * (255-alpha)/255;
}

//returns a color from RGBW value
//note that the ws281x stores colors as GRB(W)
int color_rgbw (unsigned char r, unsigned char g, unsigned char b, unsigned char w){
	return (w<<24)+(b << 16) + (g << 8) + r;
}

//returns a color from a 'color wheel' where wheelpos is the 'angle' 0-255
int deg2color(unsigned char WheelPos) {
	if(WheelPos < 85) {
		return color(255 - WheelPos * 3,WheelPos * 3 , 0);
	} else if(WheelPos < 170) {
		WheelPos -= 85;
		return color(0, 255 - WheelPos * 3, WheelPos * 3);
	} else {
		WheelPos -= 170;
		return color(WheelPos * 3, 0, 255 - WheelPos * 3);
	}
}

//returns if channel is a valid led_string index number
int is_valid_channel_number(unsigned int channel){
    return (channel >= 0) && (channel < RPI_PWM_CHANNELS) && ledstring.channel[channel].count>0 && ledstring.device!=NULL;
}

//reads key from argument buffer
//example: channel_1_count=10,
//returns channel_1_count in key buffer, then use read_val to read the 10
char * read_key(char * args, char * key, size_t size){
	size--;
    if (*args==',') args++;
	while (*args!=0 && *args!='=' && *args!=','){
		if (*args!=' ' && *args!='\t'){ //skip space
			*key=*args; //add character to key value
			key++;
			size--;
			if (size==0) break;
		}
		args++;
	}
	*key=0;
	return args;
}

//read value from command argument buffer (see read_key)
char * read_val(char * args, char * value, size_t size){
	size--;
    if (*args==',') args++;
	while (*args!=0 && *args!=','){
		if (*args!=' ' && *args!='\t'){ //skip space
			*value=*args;
			value++;
			size--;
			if (size==0) break;
		}
		args++;
	}
	*value=0;
	return args;
}

//reads color from string, returns string + 6 or 8 characters
//color_size = 3 (RGB format)  or 4 (RGBW format)
char * read_color(char * args, unsigned int * out_color, unsigned int color_size){
    unsigned char r,g,b,w;
    unsigned char color_string[8];
    unsigned int color_string_idx=0;
    *out_color = 0;
    while (*args!=0 && color_string_idx<color_size*2){
        if (*args!=' ' && *args!='\t'){ //skip space
            color_string[color_string_idx]=*args;
            color_string_idx++;
        }
        args++;
    }
    
    r = (hextable[color_string[0]]<<4) + hextable[color_string[1]];
    g = (hextable[color_string[2]]<<4) + hextable[color_string[3]];
    b = (hextable[color_string[4]]<<4) + hextable[color_string[5]];
    if (color_size==4){
        w = (hextable[color_string[6]]<<4) + hextable[color_string[7]];
        *out_color = color_rgbw(r,g,b,w);
    }else{
        *out_color = color(r,g,b);
    }
    return args;
}

//reads a hex brightness value
char * read_brightness(char * args, unsigned int * brightness){
    unsigned int idx=0;
    unsigned char str_brightness[2];
    *brightness=0;
    while (*args!=0 && idx<2){
        if (*args!=' ' && *args!='\t'){ //skip space
            brightness[idx]=*args;
            idx++;
        }
        args++;
    }
    * brightness = (hextable[str_brightness[0]] << 4) + hextable[str_brightness[1]];
    return args;
}

//initializes channels
//init <frequency>,<DMA>
void init_channels(char * args){
    char value[MAX_VAL_LEN];
    int frequency=WS2811_TARGET_FREQ, dma=5;
    
    if (ledstring.device!=NULL)	ws2811_fini(&ledstring);
    
    if (args!=NULL){
        args = read_val(args, value, MAX_VAL_LEN);
        frequency=atoi(value);
        if (*args!=0){
            args = read_val(args, value, MAX_VAL_LEN);
            dma=atoi(value);
        }
    }
    
    ledstring.dmanum=dma;
    ledstring.freq=frequency;
    if (debug) printf("Init ws2811 %d,%d\n", frequency, dma);
    ws2811_return_t ret;
    if ((ret = ws2811_init(&ledstring))!= WS2811_SUCCESS){
        fprintf(stderr, "ws2811_init failed: %s\n", ws2811_get_return_t_str(ret));
    }
}

//changes the global channel brightness
//global_brightness <channel>,<value>
void global_brightness(char * args){
    if (args!=NULL){
        char value[MAX_VAL_LEN];
        args = read_val(args, value, MAX_VAL_LEN);
        int channel = atoi(value)-1;
        if (*args!=0){
            args = read_val(args, value, MAX_VAL_LEN);
            int brightness = atoi(value);
            if (is_valid_channel_number(channel)){
                ledstring.channel[channel].brightness=brightness;
                if(debug) printf("Global brightness %d, %d\n", channel, brightness);
            }else{
                fprintf(stderr,"Invalid channel number, did you call setup and init?\n");
            }
        }
    }
}

//sets the ws2811 channels
//setup channel, led_count, type, invert, global_brightness, GPIO
void setup_ledstring(char * args){
    char value[MAX_VAL_LEN];
    int channel=0, led_count=10, type=0, invert=0, brightness=255, GPIO=18;
	
    const int led_types[]={WS2811_STRIP_RGB, //0 
                           WS2811_STRIP_RBG, //1 
                           WS2811_STRIP_GRB, //2 
                           WS2811_STRIP_GBR, //3 
                           WS2811_STRIP_BRG, //4
                           WS2811_STRIP_BGR, //5 
                           SK6812_STRIP_RGBW,//6
                           SK6812_STRIP_RBGW,//7
                           SK6812_STRIP_GRBW,//8 
                           SK6812_STRIP_GBRW,//9 
                           SK6812_STRIP_BRGW,//10 
                           SK6812_STRIP_BGRW //11
                           };
    
    
    if (args!=NULL){
        args = read_val(args, value, MAX_VAL_LEN);
        channel = atoi(value)-1;
        if (channel==2) GPIO=13;
        if (*args!=0){
            args = read_val(args, value, MAX_VAL_LEN);
            led_count = atoi(value);
            if (*args!=0){
                args = read_val(args, value, MAX_VAL_LEN);
                type = atoi(value);
                if (*args!=0){
                    args = read_val(args, value, MAX_VAL_LEN);
                    invert = atoi(value);
                    if (*args!=0){
                        args = read_val(args, value, MAX_KEY_LEN);
                        brightness = atoi(value);
                        if (*args!=0){
                            args = read_val(args, value, MAX_KEY_LEN);
                            GPIO = atoi(value);
                        }
                    }
                }
            }
        }
    }
    
    if (channel >=0 && channel < RPI_PWM_CHANNELS){

        if (debug) printf("Initialize channel %d,%d,%d,%d,%d,%d\n", channel, led_count, type, invert, brightness, GPIO);

        int color_size = 4;       
        
        switch (type){
            case WS2811_STRIP_RGB:
            case WS2811_STRIP_RBG:
            case WS2811_STRIP_GRB:
            case WS2811_STRIP_GBR:
            case WS2811_STRIP_BRG:
            case WS2811_STRIP_BGR:
                color_size=3;
                break;
        }           
        
        ledstring.channel[channel].gpionum = GPIO;
        ledstring.channel[channel].invert = invert;
        ledstring.channel[channel].count = led_count;
        ledstring.channel[channel].strip_type=led_types[type];
        ledstring.channel[channel].brightness=brightness;
        ledstring.channel[channel].color_size=color_size;
        
        int max_size=0,i;
        for (i=0; i<RPI_PWM_CHANNELS;i++){
            int color_count=4;
            switch (ledstring.channel[i].strip_type){
                case WS2811_STRIP_RGB:
                case WS2811_STRIP_RBG:
                case WS2811_STRIP_GRB:
                case WS2811_STRIP_GBR:
                case WS2811_STRIP_BRG:
                case WS2811_STRIP_BGR:
                    color_count=3;
                    break;
            }
            int size = DEFAULT_COMMAND_LINE_SIZE + ledstring.channel[i].count * 2 * color_count;
            if (size > max_size){
                max_size = size;
            }
        }
        malloc_command_line(max_size); //allocate memory for full render data    
    }else{
        if (debug) printf("Channel number %d\n", channel);
        fprintf(stderr,"Invalid channel number, use channels <number> to initialize total channels you want to use.\n");
    }
}

//prints channel settings
void print_settings(){
    unsigned int i;
    printf("DMA Freq:   %d\n", ledstring.freq); 
    printf("DMA Num:    %d\n", ledstring.dmanum);    
    for (i=0;i<RPI_PWM_CHANNELS;i++){
        printf("Channel %d:\n", i+1);
        printf("    GPIO: %d\n", ledstring.channel[i].gpionum);
        printf("    Invert: %d\n",ledstring.channel[i].invert);
        printf("    Count:  %d\n",ledstring.channel[i].count);
        printf("    Colors: %d\n", ledstring.channel[i].color_size);
        printf("    Type:   %d\n", ledstring.channel[i].strip_type);
    }
}

//sends the buffer to the leds
//render <channel>,0,AABBCCDDEEFF...
//optional the colors for leds:
//AABBCC are RGB colors for first led
//DDEEFF is RGB for second led,...
void render(char * args){
	int channel=0;
	int r,g,b,w;
	int size;
    int start;
    char value[MAX_VAL_LEN];
    char color_string[6];
    
	if (debug) printf("Render %s\n", args);
	
    if (args!=NULL){
		args = read_val(args, value, MAX_VAL_LEN);
		channel = atoi(value)-1;
        if (is_valid_channel_number(channel)){
            if (*args!=0){
                args = read_val(args, value, MAX_VAL_LEN); //read start position
                start = atoi(value);
                while (*args!=0 && (*args==' ' || *args==',')) args++; //skip white space
                
                if (debug) printf("Render channel %d selected start at %d leds %d\n", channel, start, ledstring.channel[channel].count);
                
                size = strlen(args);
                int led_count = ledstring.channel[channel].count;            
                int led_index = start % led_count;
                int color_count = ledstring.channel[channel].color_size;
                ws2811_led_t * leds = ledstring.channel[channel].leds;

                while (*args!=0){
                    unsigned int color=0;
                    args = read_color(args, & color, color_count);
                    leds[led_index].color = color;
                    led_index++;
                    if (led_index>=led_count) led_index=0;
                }
            }
        }else{
            fprintf(stderr,"Invalid channel number, did you call setup and init?\n");
        }
	}
	ws2811_render(&ledstring);
}

//shifts all colors 1 position
//rotate <channel>,<places>,<direction>,<new_color>,<new_brightness>
//if new color is set then the last led will have this color instead of the color of the first led
void rotate(char * args){
	char value[MAX_VAL_LEN];
	int channel=0, nplaces=1, direction=1;
    unsigned int new_color=0, new_brightness=255;
	int use_new_color=0;
    
	if (args!=NULL){
		args = read_val(args, value, MAX_VAL_LEN);
		channel = atoi(value)-1;
		if (*args!=0){
			args = read_val(args, value, MAX_VAL_LEN);
			nplaces = atoi(value);
			if (*args!=0){
				args = read_val(args, value, MAX_VAL_LEN);
				direction = atoi(value);
				if (*args!=0){
					args = read_val(args, value, MAX_VAL_LEN);
					if (strlen(value)>=6){
                        if (is_valid_channel_number(channel)){
                            read_color(value, & new_color, ledstring.channel[channel].color_size);
                            use_new_color=1;
                            args = read_val(args, value, MAX_VAL_LEN);
                            if (strlen(value)==2) read_brightness(value, & new_brightness);
                        }
					}
				}
			}
		}
	}
	
	if (debug) printf("Rotate %d %d %d %d\n", channel, nplaces, direction, new_color);
	
    if (is_valid_channel_number(channel)){
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
    }else{
        fprintf(stderr,"Invalid channel number, did you call setup and init?\n");
    }
}

//fills pixels with rainbow effect
//count tells how many rainbows you want
//rainbow <channel>,<count>,<startcolor>,<stopcolor>,<start>,<len>
//start and stop = color values on color wheel (0-255)
void rainbow(char * args) {
	char value[MAX_VAL_LEN];
	int channel=0, count=1,start=0,stop=255,startled=0, len=0;
	
    if (is_valid_channel_number(channel)) len=ledstring.channel[channel].count;
	if (args!=NULL){
		args = read_val(args, value, MAX_VAL_LEN);
		channel = atoi(value)-1;
        if (is_valid_channel_number(channel)) len=ledstring.channel[channel].count;;
		if (*args!=0){
			args = read_val(args, value, MAX_VAL_LEN);
			count = atoi(value);
			if (*args!=0){
				args = read_val(args, value, MAX_VAL_LEN);
				start = atoi(value);
				if (*args!=0){
					args = read_val(args, value, MAX_VAL_LEN);
					stop = atoi(value);
                    if (*args!=0){
                        args = read_val(args, value, MAX_VAL_LEN);
                        startled=atoi(value);
                        if(*args!=0){
                            args = read_val(args, value, MAX_VAL_LEN);
                            len=atoi(value);
                        }
                    }
				}
			}
		}
	}	
	
	if (is_valid_channel_number(channel)){
        if (start<0 || start > 255) start=0;
        if (stop<0 || stop > 255) stop = 255;
        if (startled<0) startled=0;
        if (startled+len> ledstring.channel[channel].count) len = ledstring.channel[channel].count-startled;
        
        if (debug) printf("Rainbow %d,%d,%d,%d,%d,%d,%d\n", channel, count,start,stop,startled,len);
        
        int numPixels = len; //ledstring.channel[channel].count;;
        int i, j;
        ws2811_led_t * leds = ledstring.channel[channel].leds;
        for(i=0; i<numPixels; i++) {
            leds[startled+i].color = deg2color(abs(stop-start) * i * count / numPixels + start);
        }
    }else{
        fprintf(stderr,"Invalid channel number, did you call setup and init?\n");
    }
}

//fills leds with certain color
//fill <channel>,<color>,<start>,<len>,<OR,AND,XOR,NOT,=>
void fill(char * args){
	char value[MAX_VAL_LEN];
    char op=0;
	int channel=0,start=0,len=-1;
	unsigned int fill_color=0;
    
	if (args!=NULL){
		args = read_val(args, value, MAX_VAL_LEN);
		channel = atoi(value)-1;
		if (*args!=0){
			args = read_val(args, value, MAX_VAL_LEN);
			if (strlen(value)>=6){
                if (is_valid_channel_number(channel)) read_color(value, & fill_color, ledstring.channel[channel].color_size);
			}else{
				printf("Invalid color\n");
			}
			if (*args!=0){
				args = read_val(args, value, MAX_VAL_LEN);
				start = atoi(value);
				if (*args!=0){
					args = read_val(args, value, MAX_VAL_LEN);
					len = atoi(value);
                    if (*args!=0){
                        args = read_val(args, value, MAX_VAL_LEN);
                        if (strcmp(value, "OR")==0) op=1;
                        else if (strcmp(value, "AND")==0) op=2;
                        else if (strcmp(value, "XOR")==0) op=3;
                        else if (strcmp(value, "NOT")==0) op=4;
                        else if (strcmp(value, "=")==0) op=0;
                    }
				}
			}
		}
	}
	
	if (is_valid_channel_number(channel)){
        if (start<0 || start>=ledstring.channel[channel].count) start=0;        
        if (len<=0 || (start+len)>ledstring.channel[channel].count) len=ledstring.channel[channel].count-start;

        if (debug) printf("fill %d,%d,%d,%d,%d\n", channel, fill_color, start, len,op);
        
        ws2811_led_t * leds = ledstring.channel[channel].leds;
        unsigned int i;
        for (i=start;i<start+len;i++){
            switch (op){
                case 0:
                    leds[i].color=fill_color;
                    break;
                case 1:
                    leds[i].color|=fill_color;
                    break;
                case 2:
                    leds[i].color&=fill_color;
                    break;
                case 3:
                    leds[i].color^=fill_color;
                    break;
                case 4:
                    leds[i].color=~leds[i].color;
                    break;
            }
        }
    }else{
        fprintf(stderr,"Invalid channel number, did you call setup and init?\n");
    }
}

//dims leds
//brightness <channel>,<brightness>,<start>,<len> (brightness: 0-255)
void brightness(char * args){
	char value[MAX_VAL_LEN];
	int channel=0, brightness=255;
	unsigned int start=0, len=0;
    if (is_valid_channel_number(channel)){
        len = ledstring.channel[channel].count;;
    }
	if (args!=NULL){
		args = read_val(args, value, MAX_VAL_LEN);
		channel = atoi(value)-1;
        if (is_valid_channel_number(channel)) len = ledstring.channel[channel].count;;
		if (*args!=0){
			args = read_val(args, value, MAX_VAL_LEN);
			brightness = atoi(value);
            if(*args!=0){
                args = read_val(args, value, MAX_VAL_LEN);
                start = atoi(value);
                if (*args!=0){
                    args = read_val(args, value, MAX_VAL_LEN);
                    len = atoi(value);
                }
            }
		}
	}
	
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
        fprintf(stderr,"Invalid channel number, did you call setup and init?\n");
    }
}

//causes a fade effect in time
//fade <channel>,<startbrightness>,<endbrightness>,<delay>,<step>,<startled>,<len>
void fade (char * args){
    char value[MAX_VAL_LEN];
	int channel=0, brightness=255,step=1,startbrightness=0, endbrightness=255;
	unsigned int start=0, len=0, delay=50;
    
    if (is_valid_channel_number(channel)){
        len = ledstring.channel[channel].count;;
    }
    if (args!=NULL){
        args = read_val(args, value, MAX_VAL_LEN);
		channel = atoi(value)-1;
        if (is_valid_channel_number(channel)){
            len = ledstring.channel[channel].count;;
        }
        if (*args!=0){
            args = read_val(args, value, MAX_VAL_LEN);
            startbrightness=atoi(value);
            if(*args!=0){
                args = read_val(args, value, MAX_VAL_LEN);
                endbrightness=atoi(value);
                if(*args!=0){
                    args = read_val(args, value, MAX_VAL_LEN);
                    delay = atoi(value);
                    if(*args!=0){
                        args = read_val(args, value, MAX_VAL_LEN);
                        step = atoi(value);
                        if(*args!=0){
                            args = read_val(args, value, MAX_VAL_LEN);
                            start=atoi(value);
                            if (*args!=0){
                                args = read_val(args, value, MAX_VAL_LEN);
                                len = atoi(value);
                            }
                        }
                    }
                }
            }
        }
    }
            
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
        } 
    }else{
        fprintf(stderr,"Invalid channel number, did you call setup and init?\n");
    }
}


//makes some leds blink between 2 given colors for x times with a given delay
//blink <channel>,<color1>,<color2>,<delay>,<blink_count>,<startled>,<len>
void blink (char * args){
    char value[MAX_VAL_LEN];
	int channel=0, color1=0, color2=0xFFFFFF,delay=1000, count=10;
	unsigned int start=0, len=0;
    
    if (is_valid_channel_number(channel)){
        len = ledstring.channel[channel].count;;
    }
    if (args!=NULL){
        args = read_val(args, value, MAX_VAL_LEN);
		channel = atoi(value)-1;
        if (is_valid_channel_number(channel)){
            len = ledstring.channel[channel].count;;
        }
        if (*args!=0){
            args = read_val(args, value, MAX_VAL_LEN);
			if (strlen(value)>=6){
                if (is_valid_channel_number(channel)) read_color(value, & color1, ledstring.channel[channel].color_size);
			}else{
				printf("Invalid color 1\n");
			}
            if(*args!=0){
                args = read_val(args, value, MAX_VAL_LEN);
				if (strlen(value)>=6){
					if (is_valid_channel_number(channel)) read_color(value, & color2, ledstring.channel[channel].color_size);
				}else{
					printf("Invalid color 2\n");
				}
                if(*args!=0){
                    args = read_val(args, value, MAX_VAL_LEN);
                    delay = atoi(value);
                    if(*args!=0){
                        args = read_val(args, value, MAX_VAL_LEN);
                        count = atoi(value);
                        if(*args!=0){
                            args = read_val(args, value, MAX_VAL_LEN);
                            start=atoi(value);
                            if (*args!=0){
                                args = read_val(args, value, MAX_VAL_LEN);
                                len = atoi(value);
                            }
                        }
                    }
                }
            }
        }
    }
            
	if (is_valid_channel_number(channel)){

        if (start>=ledstring.channel[channel].count) start=0;
        if ((start+len)>ledstring.channel[channel].count) len=ledstring.channel[channel].count-start;
        
        if (delay<=0) delay=100;
        
        if (debug) printf("blink %d, %d, %d, %d, %d, %d, %d\n", channel, color1, color2, delay, count, start, len);
        
        ws2811_led_t * leds = ledstring.channel[channel].leds;
        int i,blinks;
        for (blinks=0; blinks<count;blinks++){
            for (i=start;i<start+len;i++){
                if ((blinks%2)==0) {
					leds[i].color=color1;
				}else{
					leds[i].color=color2;
				}
            }
            ws2811_render(&ledstring);
            usleep(delay * 1000);
        } 
    }else{
        fprintf(stderr,"Invalid channel number, did you call setup and init?\n");
    }
}

//generates a brightness gradient pattern of a color component or brightness level
//gradient <channel>,<RGBWL>,<startlevel>,<endlevel>,<startled>,<len>
void gradient (char * args){
    char value[MAX_VAL_LEN];
	int channel=0, startlevel=0,endlevel=255;
	unsigned int start=0, len=0;
    char component='L'; //L is brightness level
    
    if (is_valid_channel_number(channel)){
        len = ledstring.channel[channel].count;;
    }
    if (args!=NULL){
        args = read_val(args, value, MAX_VAL_LEN);
		channel = atoi(value)-1;
        if (is_valid_channel_number(channel)){
            len = ledstring.channel[channel].count;;
        }
        if (*args!=0){
            args = read_val(args, value, MAX_VAL_LEN);
            component=toupper(value[0]);
            if(*args!=0){
                args = read_val(args, value, MAX_VAL_LEN);
                startlevel=atoi(value);
                if(*args!=0){
                    args = read_val(args, value, MAX_VAL_LEN);
                    endlevel = atoi(value);
                    if(*args!=0){
                        args = read_val(args, value, MAX_VAL_LEN);
                        start=atoi(value);
                        if (*args!=0){
                            args = read_val(args, value, MAX_VAL_LEN);
                            len = atoi(value);
                        }
                    }
                }
            }
        }
    }
            
	if (is_valid_channel_number(channel)){
        if (startlevel>0xFF) startlevel=255;
        if (endlevel>0xFF) endlevel=255;
        
        if (start>=ledstring.channel[channel].count) start=0;
        if ((start+len)>ledstring.channel[channel].count) len=ledstring.channel[channel].count-start;
        
        
        float step = 1.0*(endlevel-startlevel) / (float)(len-1);
        
        if (debug) printf("gradient %d, %c, %d, %d, %d,%d\n", channel, component, startlevel, endlevel, start,len);
        
        ws2811_led_t * leds = ledstring.channel[channel].leds;
        
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
        fprintf(stderr,"Invalid channel number, did you call setup and init?\n");
    }
}

//generates random colors
//random <channel>,<start>,<len>,<RGBWL>
void add_random(char * args){
    char value[MAX_VAL_LEN];
	int channel=0;
	unsigned int start=0, len=0;
    char component='L'; //L is brightness level
    int use_r=1, use_g=1, use_b=1, use_w=1, use_l=1;
    
    if (is_valid_channel_number(channel)){
        len = ledstring.channel[channel].count;;
    }
    if (args!=NULL){
        args = read_val(args, value, MAX_VAL_LEN);
		channel = atoi(value)-1;
        if (is_valid_channel_number(channel)){
            len = ledstring.channel[channel].count;;
        }
        if(*args!=0){
            args = read_val(args, value, MAX_VAL_LEN);
            start=atoi(value);
            if (*args!=0){
                args = read_val(args, value, MAX_VAL_LEN);
                len = atoi(value);
                if (*args!=0){
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
        fprintf(stderr,"Invalid channel number, did you call setup and init?\n");
    }
}

void start_loop (char * args){
    if (mode==MODE_FILE){
        if (loop_index>=MAX_LOOPS){
            loop_index=MAX_LOOPS-1;
            printf("Warning max nested loops reached!\n");
            return;
        }
        if (debug) printf ("do %d\n", ftell(input_file));
        loops[loop_index].do_pos = ftell(input_file);
        loops[loop_index].n_loops=0;
        loop_index++;
    }else if (mode==MODE_TCP){
        if (loop_index<MAX_LOOPS){
            if (debug) printf ("do %d\n", thread_read_index);
            loops[loop_index].do_pos = thread_read_index;
            loops[loop_index].n_loops=0;
            loop_index++;
        }else{
            printf("Warning max nested loops reached!\n");
        }
    }
}

void end_loop(char * args){
    int max_loops = 0; //number of wanted loops
    if (args!=NULL){
        max_loops = atoi(args);
    }
    if (mode==MODE_FILE){
        if (debug) printf ("loop %d \n", ftell(input_file));
        if (loop_index==0){ //no do found!
            fseek(input_file, 0, SEEK_SET);
        }else{
            loops[loop_index-1].n_loops++;
            if (max_loops==0 || loops[loop_index-1].n_loops<max_loops){ //if number of loops is 0 = loop forever
                fseek(input_file, loops[loop_index-1].do_pos,SEEK_SET);
            }else{
                if (loop_index>0) loop_index--; //exit loop
            }
        }
    }else if (mode==MODE_TCP){
        if (debug) printf("loop %d\n", thread_read_index);
        if (loop_index==0){
            thread_read_index=0; 
        }else{
            loops[loop_index-1].n_loops++;
            if (max_loops==0 || loops[loop_index-1].n_loops<max_loops){ //if number of loops is 0 = loop forever
                thread_read_index = loops[loop_index-1].do_pos;
            }else{
                if (loop_index>0) loop_index--; //exit loop
            }       
        }
    }
}

//read JPEG image and put pixel data to LEDS
//readjpg <channel>,<FILE>,<start>,<len>,<offset>,<OR AND XOR NOT =>
//offset = where to start in JPEG file
#ifdef USE_JPEG
void readjpg(char * args){
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	
	char value[MAX_VAL_LEN];
	int channel=0;
	char filename[MAX_VAL_LEN];
	unsigned int start=0, len=0, offset=0;
	int op=0;
    
    if (is_valid_channel_number(channel)){
        len = ledstring.channel[channel].count;;
    }
    if (args!=NULL){
        args = read_val(args, value, MAX_VAL_LEN);
		channel = atoi(value)-1;
        if (is_valid_channel_number(channel)){
            len = ledstring.channel[channel].count;;
        }
        if(*args!=0){
			args = read_val(args, value, MAX_VAL_LEN);
			strcpy(filename, value);
			if (*args!=0){
				args = read_val(args, value, MAX_VAL_LEN);
				start=atoi(value);
				if (*args!=0){
					args = read_val(args, value, MAX_VAL_LEN);
					len = atoi(value);
					if (*args!=0){
						args = read_val(args, value, MAX_VAL_LEN);
						offset = atoi(value);						
						if (*args!=0){
							args = read_val(args, value, MAX_VAL_LEN);
							if (strcmp(value, "OR")==0) op=1;
							else if (strcmp(value, "AND")==0) op=2;
							else if (strcmp(value, "XOR")==0) op=3;
							else if (strcmp(value, "NOT")==0) op=4;
						}
					}
				}
			}
        }
    }
    
    if (is_valid_channel_number(channel)){
		FILE * infile;		/* source file */
		int row_stride;		/* physical row width in output buffer */

		if (debug) printf("readjpg %d,%s,%d,%d,%d,%d\n", channel, filename, start, len,offset,op);
		
		if ((infile = fopen(filename, "rb")) == NULL) {
			fprintf(stderr, "Error: can't open %s\n", filename);
			return;
		}

		// We set up the normal JPEG error routines, then override error_exit.
		cinfo.err = jpeg_std_error(&jerr.pub);
		jerr.pub.error_exit = my_error_exit;
		// Establish the setjmp return context for my_error_exit to use.
		if (setjmp(jerr.setjmp_buffer)) {
			/* If we get here, the JPEG code has signaled an error.
			 * We need to clean up the JPEG object, close the input file, and return.
			 */
			jpeg_destroy_decompress(&cinfo);
			fclose(infile);
			return;
		}
		
		// Now we can initialize the JPEG decompression object.
		jpeg_create_decompress(&cinfo);
		jpeg_stdio_src(&cinfo, infile);

		jpeg_read_header(&cinfo, TRUE);
		jpeg_start_decompress(&cinfo);

		row_stride = cinfo.output_width * cinfo.output_components;
		
		JSAMPARRAY buffer;	// Output row buffer
		int i=0,jpg_idx=0,led_idx; //pixel index for current row, jpeg image, led string
		buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

		ws2811_led_t * leds = ledstring.channel[channel].leds;
		
		if (start>=ledstring.channel[channel].count) start=0;
		if ((start+len)>ledstring.channel[channel].count) len=ledstring.channel[channel].count-start;
		
		led_idx=start; //start at this led index
		
		while (cinfo.output_scanline < cinfo.output_height) {
			jpeg_read_scanlines(&cinfo, buffer, 1);
			for(i=0;i<row_stride;i++){
				if (jpg_idx>=offset){ //check jpeg offset
					unsigned char r,g,b;
					r = buffer[0][i*cinfo.output_components];
					g = buffer[0][i*cinfo.output_components+1];
					b = buffer[0][i*cinfo.output_components+2];
					if (cinfo.output_components==1){ //grayscale image
						g = r;
						b = r;
					}
					if (led_idx<len){
						if (debug) printf("led %d= r %d,g %d,b %d, jpg idx=%d\n", led_idx, r, g, b,jpg_idx);
						int fill_color = color(r,g,b);
						switch (op){
							case 0:
								leds[led_idx].color=fill_color;
								break;
							case 1:
								leds[i].color|=fill_color;
								break;
							case 2:
								leds[i].color&=fill_color;
								break;
							case 3:
								leds[i].color^=fill_color;
								break;
							case 4:
								leds[i].color=~fill_color;
								break;
						}
					}
					led_idx++;
				}
				jpg_idx++;
			}
		}

		jpeg_finish_decompress(&cinfo);
		jpeg_destroy_decompress(&cinfo);
		fclose(infile);
	}
}
#endif

//read PNG image and put pixel data to LEDS
//readjpg <channel>,<FILE>,<BACKCOLOR>,<start>,<len>,<offset>,<OR AND XOR =>
//offset = where to start in PNG file
//backcolor = color to use for transparent area, FF0000 = BLUE
//P = use the PNG backcolor (default)
//W = use the alpha data for the White leds in RGBW LED strips
#ifdef USE_PNG
void readpng(char * args){
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	
	char value[MAX_VAL_LEN];
	int channel=0;
	char filename[MAX_VAL_LEN];
	unsigned int start=0, len=0, offset=0;
	int op=0;
	int backcolor=0;
    int backcolortype=0; //0 = use PNG backcolor, 1 = use given backcolor, 2 = no backcolor but use alpha for white leds
	
	//read function arguments
    if (is_valid_channel_number(channel)){
        len = ledstring.channel[channel].count;;
    }
    if (args!=NULL){
        args = read_val(args, value, MAX_VAL_LEN);
		channel = atoi(value)-1;
        if (is_valid_channel_number(channel)){
            len = ledstring.channel[channel].count;;
        }
        if(*args!=0){
			args = read_val(args, value, MAX_VAL_LEN);
			strcpy(filename, value);
			if (*args!=0){
				args = read_val(args, value, MAX_VAL_LEN);
				if (strlen(value)>=6){
					if (is_valid_channel_number(channel)){
						read_color(value, & backcolor, ledstring.channel[channel].color_size);
						backcolortype=1;
					}
				}else if (strcmp(value, "W")==0){
					backcolortype=2;
				}
				if (*args!=0){
					args = read_val(args, value, MAX_VAL_LEN);
					start=atoi(value);
					if (*args!=0){
						args = read_val(args, value, MAX_VAL_LEN);
						len = atoi(value);
						if (*args!=0){
							args = read_val(args, value, MAX_VAL_LEN);
							offset = atoi(value);						
							if (*args!=0){
								args = read_val(args, value, MAX_VAL_LEN);
								if (strcmp(value, "OR")==0) op=1;
								else if (strcmp(value, "AND")==0) op=2;
								else if (strcmp(value, "XOR")==0) op=3;
								else if (strcmp(value, "NOT")==0) op=4;
							}
						}
					}
				}
			}
        }
    }
	
	if (is_valid_channel_number(channel)){
		FILE * infile;		/* source file */
		ulg image_width, image_height, image_rowbytes;
		int image_channels,rc;
		uch *image_data;
		uch bg_red=0, bg_green=0, bg_blue=0;

		if (debug) printf("readpng %d,%s,%d,%d,%d,%d,%d\n", channel, filename, backcolor, start, len,offset,op);
		
		if ((infile = fopen(filename, "rb")) == NULL) {
			fprintf(stderr, "Error: can't open %s\n", filename);
			return;
		}
		
		if ((rc = readpng_init(infile, &image_width, &image_height)) != 0) {
            switch (rc) {
                case 1:
                    fprintf(stderr, "[%s] is not a PNG file: incorrect signature.\n", filename);
                    break;
                case 2:
                    fprintf(stderr, "[%s] has bad IHDR (libpng longjmp).\n", filename);
                    break;
                case 4:
                    fprintf(stderr, "Read PNG insufficient memory.\n");
                    break;
                default:
                    fprintf(stderr, "Unknown readpng_init() error.\n");
                    break;
            }
            fclose(infile);
			return;
        }
		
		//get the background color (for transparency support)
		if (backcolortype==0){
			if (readpng_get_bgcolor(&bg_red, &bg_green, &bg_blue) > 1){
				readpng_cleanup(TRUE);
				fclose(infile);
				fprintf(stderr, "libpng error while checking for background color\n");
				return;
			}
		}else{
			bg_red = get_red(backcolor);
			bg_green = get_green(backcolor);
			bg_blue = get_blue(backcolor);
		}
		
		//read entire image data
		image_data = readpng_get_image(2.2, &image_channels, &image_rowbytes);
		
		readpng_cleanup(FALSE);
		fclose(infile);
		
		if (image_data) {
			int row=0, led_idx=0, png_idx=0, i=0;
			uch r, g, b, a;
			uch *src;
			
			ws2811_led_t * leds = ledstring.channel[channel].leds;
		
			if (start>=ledstring.channel[channel].count) start=0;
			if ((start+len)>ledstring.channel[channel].count) len=ledstring.channel[channel].count-start;
			
			led_idx=start; //start at this led index
			//load all pixels
			for (row = 0;  row < image_height; row++) {
				src = image_data + row * image_rowbytes;
				
				for (i = image_width;  i > 0;  --i) {
					r = *src++;
					g = *src++;
					b = *src++;
					
					if (image_channels != 3){
						a = *src++;
						if (backcolortype!=2){
							r = alpha_component(r, bg_red,a);
							g = alpha_component(g, bg_green,a);
							b = alpha_component(b, bg_blue,a);
						}
					}					
					if (png_idx>=offset){
						if (debug) printf("led %d= r %d,g %d,b %d,a %d, PNG channels=%d, PNG idx=%d\n", led_idx, r, g, b, a,image_channels,png_idx);
						if (led_idx<len){
							int fill_color;
							if (backcolortype==2 && ledstring.channel[channel].color_size>3){
								fill_color=color_rgbw(r,g,b,a);
							}else{
								fill_color=color(r,g,b);
							}
		
							switch (op){
								case 0:
									leds[led_idx].color=fill_color;
									break;
								case 1:
									leds[i].color|=fill_color;
									break;
								case 2:
									leds[i].color&=fill_color;
									break;
								case 3:
									leds[i].color^=fill_color;
									break;
								case 4:
									leds[i].color=~fill_color;
									break;
							}
							
						}
						led_idx++;
					}
					png_idx++;
				}
			}
		}else{
			fprintf(stderr, "Unable to decode PNG image\n");
		}
    }
}
#endif

//initializes the memory for a TCP/IP multithread buffer
void init_thread(char * data){
    if (thread_data==NULL){
        thread_data = (char *) malloc(DEFAULT_BUFFER_SIZE);
        thread_data_size = DEFAULT_BUFFER_SIZE;
    }                
    start_thread=0;
    thread_read_index=0;
    thread_write_index=0;
    thread_running=0;
    write_to_thread_buffer=1; //from now we save all commands to the thread buffer
}

//expands the TCP/IP multithread buffer (increase size by 2)
void expand_thread_data_buffer(){
    thread_data_size = thread_data_size * 2;
    char * tmp_buffer = (char *) malloc(thread_data_size);
    memcpy((void*) tmp_buffer, (void*)thread_data, thread_data_size);
    free(thread_data);
    thread_data = tmp_buffer;
}

//adds data to the thread buffer 
void write_thread_buffer (char c){
    thread_data[thread_write_index] = c;
    thread_write_index++;
    if (thread_write_index==thread_data_size) expand_thread_data_buffer();
}

//this function can be run in other thread for TCP/IP to enable do ... loops  (usefull for websites)
void thread_func (void * param){
    thread_read_index=0;
    if (debug) printf("Enter thread %d,%d,%d.\n", thread_running,thread_read_index,thread_write_index);
    while (thread_running){
        char c = thread_data[thread_read_index];
        //if(debug) printf("Process char %c %d\n", c, thread_read_index);
        process_character(c);
        thread_read_index++;
        if (thread_read_index>=thread_write_index) break; //exit loop if we are at the end of the file
    }
    thread_running=0;
    if (debug) printf("Exit thread.\n");
    pthread_exit(NULL); //exit the tread
}

void str_replace(char * dst, char * src, char * find, char * replace){
	char *p;
	size_t replace_len = strlen(replace);
	size_t find_len = strlen(find);
	
	p = strstr(src, find);
	while (p){
		strncpy(dst, src, p - src); //copy first part
		dst+=p-src;
		strcpy(dst, replace);
		dst+=replace_len;
		p+=find_len;
		src=p;
		p = strstr(p, find);
	}
	if (*src!='\0') strcpy(dst, src); //copy last part
}

//executes 1 command line
void execute_command(char * command_line){
    
    if (command_line[0]=='#') return; //=comments
    
    if (write_to_thread_buffer){
        if (strncmp(command_line, "thread_stop", 11)==0){
            if (mode==MODE_TCP){
                write_to_thread_buffer=0;
                if (debug) printf("Thread stop.\n");
                if (thread_write_index>0) start_thread=1; //remember to start the thread when client closes the TCP/IP connection
            }        
        }else{
            if (debug) printf("Write to thread buffer: %s\n", command_line);
            while (*command_line!=0){
                write_thread_buffer(*command_line); //for TCP/IP we write to the thread buffer
                command_line++;
            }
            write_thread_buffer(';');
        }
    }else{
		char * raw_args = strchr(command_line, ' ');		
        char * command =  strtok(command_line, " \r\n");

		char * arg = NULL;
		
		if (raw_args!=NULL){
			raw_args++;			
			if (strlen(raw_args)>0){
				arg = (char*) malloc(strlen(raw_args)*2);
				char * tmp_arg = (char *) malloc(strlen(raw_args)*2);
				int i=0;
				char find_loop_nr[MAX_LOOPS+2];
				char replace_loop_index[MAX_LOOPS+2];
				strcpy(arg,raw_args);
				for (i=0;i<loop_index;i++){
					sprintf(find_loop_nr, "{%d}", i);
					sprintf(replace_loop_index, "%d", loops[i].n_loops);
					str_replace(tmp_arg, arg, find_loop_nr, replace_loop_index); //cannot put result in same string we are replacing, store in temp buffer
					strcpy(arg, tmp_arg);
				}
				free(tmp_arg);
			}
		}
        
        if (strcmp(command, "render")==0){
            render(arg);
        }else if (strcmp(command, "rotate")==0){
            rotate(arg);
        }else if (strcmp(command, "delay")==0){
            if (arg!=NULL)	usleep((atoi(arg)+1)*1000);
        }else if (strcmp(command, "brightness")==0){
            brightness(arg);
        }else if (strcmp(command, "rainbow")==0){
            rainbow(arg);
        }else if (strcmp(command, "fill")==0){	
            fill(arg);
        }else if (strcmp(command, "fade")==0){
            fade(arg);
        }else if (strcmp(command, "gradient")==0){
            gradient(arg);
        }else if (strcmp(command, "random")==0){
            add_random(arg);
        }else if (strcmp(command, "do")==0){
            start_loop(arg);
        }else if (strcmp(command, "loop")==0){
            end_loop(arg);
        }else if (strcmp(command, "thread_start")==0){ //start a new thread that processes code
            if (thread_running==0 && mode==MODE_TCP) init_thread(arg);
        }else if (strcmp(command, "init")==0){ //first init ammount of channels wanted
            init_channels(arg);
        }else if (strcmp(command, "setup")==0){ //setup the channels
            setup_ledstring(arg);
        }else if (strcmp(command, "settings")==0){
            print_settings();
        }else if (strcmp(command, "global_brightness")==0){
            global_brightness(arg);
		}else if (strcmp(command, "blink")==0){
			blink(arg);
		#ifdef USE_JPEG
		}else if (strcmp(command, "readjpg")==0){
			readjpg(arg);
		#endif
		#ifdef USE_PNG
		}else if (strcmp(command, "readpng")==0){
			readpng(arg);
		#endif
        }else if (strcmp(command, "help")==0){
            printf("debug (enables some debug output)\n");
            printf("setup <channel>, <led_count>, <led_type>, <invert>, <global_brightness>, <gpionum>\n");
            printf("    led types:\n");
            printf("     0 WS2811_STRIP_RGB\n");
            printf("     1  WS2811_STRIP_RBG\n");
            printf("     2  WS2811_STRIP_GRB\n"); 
            printf("     3  WS2811_STRIP_GBR\n"); 
            printf("     4  WS2811_STRIP_BRG\n");
            printf("     5  WS2811_STRIP_BGR,\n"); 
            printf("     6  SK6812_STRIP_RGBW\n");
            printf("     7  SK6812_STRIP_RBGW\n");
            printf("     8  SK6812_STRIP_GRBW\n"); 
            printf("     9  SK6812_STRIP_GBRW\n");
            printf("     10 SK6812_STRIP_BRGW\n");
            printf("     11 SK6812_STRIP_BGRW\n");
            printf("init <frequency>,<DMA> (initializes PWM output, call after all setup commands)\n");
            printf("render <channel>,<start>,<RRGGBBWWRRGGBBWW>\n");
            printf("rotate <channel>,<places>,<direction>,<new_color>,<new_brightness>\n");
            printf("rainbow <channel>,<count>,<start_color>,<stop_color>,<start_led>,<len>\n");
            printf("fill <channel>,<color>,<start>,<len>,<OR,AND,XOR,NOT,=>\n");
            printf("brightness <channel>,<brightness>,<start>,<len> (brightness: 0-255)\n");
            printf("fade <channel>,<start_brightness>,<end_brightness>,<delay ms>,<step>,<start_led>,<len>\n");
            printf("gradient <channel>,<RGBWL>,<start_level>,<end_level>,<start_led>,<len>\n");
            printf("random <channel>,<start>,<len>,<RGBWL>\n");
			#ifdef USE_JPEG
			printf("readjpg <channel>,<file>,<LED start>,<len>,<JPEG Pixel offset>,<OR,AND,XOR,NOT,=>\n");
			#endif
			#ifdef USE_PNG
			printf("readpng <channel>,<file>,<BACKCOLOR>,<LED start>,<len>,<PNG Pixel offset>,<OR,AND,XOR,NOT,=>\n     BACKCOLOR=XXXXXX for color, PNG=USE PNG Back color (default), W=Use alpha for white leds in RGBW strips.\n");
			#endif
            printf("settings\n");
            printf("do ... loop (TCP / File mode only)\n");
			printf("Inside a finite loop {x} will be replaced by the current loop index number. x stands for the loop number in case of multiple nested loops (default use 0).");
            printf("exit\n");
        }else if (strcmp(command, "debug")==0){
            if (debug) debug=0;
            else debug=1;
        }else if (strcmp(command, "exit")==0){
            printf("Exiting.\n");
            exit_program=1;
        }else{
            printf("Unknown cmd: %s\n", command_line);
        }
		//free(arg);
    }
}

void process_character(char c){
    if (c=='\n' || c == '\r' || c == ';'){
        if (command_index>0){
            command_line[command_index]=0; //terminate with 0
            execute_command(command_line);
            command_index=0;
        }
    }else{
        if (!(command_index==0 && c==' ')){
            command_line[command_index]=(char)c;
            command_index++;
            if (command_index==command_line_size) command_index=0;
        }
    }
}

//for information see:
//http://www.linuxhowtos.org/C_C++/socket.htm
//waits for client to connect
void tcp_wait_connection (){
    socklen_t clilen;
    int sock_opt = 1;
    socklen_t optlen = sizeof (sock_opt);
    
    if (start_thread){
        if (debug) printf("Running thread.\n");
        thread_running=1; //thread will run untill thread_running becomes 0 (this is after a new client has connected)
        pthread_create(& thread, NULL, (void* (*)(void*)) & thread_func, NULL);
    }    
    
    printf("Waiting for client to connect.\n");
    
    clilen = sizeof(cli_addr);
    active_socket = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
    
    if (setsockopt(active_socket, SOL_SOCKET, SO_KEEPALIVE, &sock_opt, optlen)) printf("Error set SO_KEEPALIVE\n");
    
    if (thread_running){//if there is a thread active we exit it 
        thread_running=0;
        pthread_join(thread,NULL); //wait for thread to finish and exit
    }
    
    write_to_thread_buffer=0;
    thread_write_index=0;
    thread_read_index=0;
    start_thread=0;
     
    printf("Client connected.\n");
}

//sets up sockets
void start_tcpip(int port){
     sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
     if (sockfd < 0) {
        fprintf(stderr,"ERROR opening socket\n");
        exit(1);
     }

     bzero((char *) &serv_addr, sizeof(serv_addr));

     serv_addr.sin_family = AF_INET;
     serv_addr.sin_addr.s_addr = INADDR_ANY;
     serv_addr.sin_port = htons(port);
     if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        fprintf(stderr,"ERROR on binding.\n");
        exit(1);
     }
     listen(sockfd,5);
     tcp_wait_connection();
}

//main routine
int main(int argc, char *argv[]){
    int ret = 0;
	int i;
	int index=0;
    
    srand (time(NULL));

    ledstring.device=NULL;
    for (i=0;i<RPI_PWM_CHANNELS;i++){
        ledstring.channel[0].gpionum = 0;
        ledstring.channel[0].count = 0;
        ledstring.channel[1].gpionum = 0;
        ledstring.channel[1].count = 0;
    }
    
	command_line = NULL;
    named_pipe_file=NULL;
	malloc_command_line(DEFAULT_COMMAND_LINE_SIZE);

    setup_handlers();

    input_file = stdin; //by default we read from console, stdin
    mode = MODE_STDIN;
    
	int arg_idx=1;
	while (argc>arg_idx){
        if (strcmp(argv[arg_idx], "-p")==0){ //use a named pipe, creates a file (by default in /dev/ws281x) which you can write commands to: echo "command..." > /dev/ws281x
            if (argc>arg_idx+1){
                named_pipe_file = (char*)malloc(strlen(argv[arg_idx+1])+1);
                strcpy(named_pipe_file,argv[arg_idx+1]);
				arg_idx++;
            }else{
                named_pipe_file = (char*)malloc(strlen(DEFAULT_DEVICE_FILE)+1);
                strcpy(named_pipe_file, DEFAULT_DEVICE_FILE);
            }
            printf ("Opening %s as named pipe.", named_pipe_file);
            remove(named_pipe_file);
            mkfifo(named_pipe_file,0777);
            chmod(named_pipe_file,0777);
            input_file = fopen(named_pipe_file, "r");
            mode  = MODE_NAMED_PIPE;
        }else if (strcmp(argv[arg_idx], "-f")==0){ //read commands / data from text file
            if (argc>arg_idx+1){
                input_file = fopen(argv[arg_idx+1], "r");
                printf("Opening %s.", argv[arg_idx+1]);
				arg_idx++;
            }else{
                printf("Error you must enter a file name after -f option\n");
                exit(1);
            }
            mode = MODE_FILE;
        }else if (strcmp(argv[arg_idx], "-tcp")==0){ //open up tcp ip port and read commands from there
            if (argc>arg_idx+1){
                int port = atoi(argv[arg_idx+1]);
                if (port==0) port=9999;
				arg_idx++;
                printf("Listening on %d.\n", port);
                start_tcpip(port);
            }else{
                printf("You must enter a port after -tcp option\n");
                exit(1);
            }
            mode = MODE_TCP;
        }else if (strcmp(argv[arg_idx], "-d")==0){ //turn debug on
			debug=1;
		}else if (strcmp(argv[arg_idx], "-i")==0){ //initialize command
			if (argc>arg_idx+1){
				arg_idx++;
				for(i=0;i<strlen(argv[arg_idx]);i++){
					process_character(argv[arg_idx][i]);
				}
			}
		}else if (strcmp(argv[arg_idx], "-?")==0){
			printf("Command line options:\n");
			printf("-p <pipename>       creates a named pipe at location <pipename> where you can write command to.\n");
			printf("-f <filename>       read commands from <filename>\n");
			printf("-tcp <port>         listen for TCP connection to receive commands from.\n");
			printf("-d                  turn debug output on.\n");
			printf("-i \"<commands>\"       initialize with <commands> (seperate and end with ;)\n");
			printf("-?                  show this message.\n");
			return 0;
		}
		arg_idx++;
	}
	
	if ((mode == MODE_FILE || mode == MODE_NAMED_PIPE) && input_file==NULL){
		fprintf(stderr,"Error opening file!\n");
		exit(1);
	}
	
    int c;
	
	while (exit_program==0) {
        if (mode==MODE_TCP){
            c = 0;
            if (read(active_socket, (void *) & c, 1)<=0) c = EOF; //returns 0 if connection is closed, -1 if no more data available and >0 if data read
        }else{
            c = fgetc (input_file); //doesn't work with tcp
        }
        
	  if (c!=EOF){
        process_character(c);
	  }else{
        //end of file or read error
		switch (mode){
            case MODE_TCP:
                if (!exit_program){
                    tcp_wait_connection(); //go back to wait for connection
                }
                break;
            case MODE_NAMED_PIPE:
            case MODE_STDIN:
                usleep(10000);
                break;
            case MODE_FILE:
                process_character('\n'); //end last line
                if (ftell(input_file)==feof(input_file))  exit_program=1; //exit the program if we reached the end
                break;
        }
	  }
    }
	
    if (mode==MODE_TCP){
        shutdown(active_socket,SHUT_RDWR);
        shutdown(sockfd,SHUT_RDWR);
        close(active_socket);
        close(sockfd);
    }else{
        fclose(input_file);
    }
        
    if (named_pipe_file!=NULL){
        remove(named_pipe_file);
        free(named_pipe_file);
    }
	free(command_line);
    if (thread_data!=NULL) free(thread_data);
    if (ledstring.device!=NULL) ws2811_fini(&ledstring);
    
    return ret;
}