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
#include <sys/time.h>
#include <signal.h>
#include <pthread.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <math.h> 
#include "gifdec.h"
#include "ws2811.h"
#include "sk9822.h"

//#define USE_2D

//enables 2D and cairo graphics
#ifdef USE_2D
	#include <cairo.h>
	#include <cairo-ft.h>
	#include <ft2build.h>
#endif 


#ifdef USE_AUDIO
	//#include <portaudio.h>
	#include <alsa/asoundlib.h>
#endif


#define DEFAULT_DEVICE_FILE "/dev/ws281x"
#define DEFAULT_COMMAND_LINE_SIZE 1024
#define DEFAULT_BUFFER_SIZE 32768
#define MAX_CHANNELS 32

#define MAX_KEY_LEN 255
#define MAX_VAL_LEN 255
#define MAX_LOOPS 64
#define MAX_THREADS 64

#define MODE_STDIN 0
#define MODE_NAMED_PIPE 1
#define MODE_FILE 2
#define MODE_TCP 3

#define ERROR_INVALID_CHANNEL "Invalid channel number, did you call setup and init?\n"
#define ERROR_INVALID_2D_CHANNEL "Invalid channel number, did you call setup and init?\n"

#define CHANNEL_TYPE_NONE 0
#define CHANNEL_TYPE_WS2811 1
#define CHANNEL_TYPE_SK9822 2
#define CHANNEL_TYPE_VIRTUAL 99

#define DEFAULT_FONT "monospace"

#define COLOR_TRANSPARENT 0xFF000000

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

typedef struct {
    int do_pos;
    int n_loops; //number of loops made (index)
	int add_to_index; //add this value to the current index variable
} do_loop;


FILE *			input_file;         		//the named pipe handle
char *			named_pipe_file;    		//holds named pipe file name 
char *			initialize_cmd=NULL; 		//initialze command
int				mode=MODE_STDIN;          //mode we operate in (TCP, named pipe, file, stdin)
int				debug=0;            		//set to 1 to enable debug output
volatile int	exit_program=0;			//set to 1 to exit the program

//for TCP mode
int sockfd;        //socket that listens
int active_socket=-1; //current active connection socket
socklen_t clilen;
struct sockaddr_in serv_addr, cli_addr;
int port=0;

#define JOIN_THREAD_CANCEL 0
#define JOIN_THREAD_WAIT 1

#ifdef USE_AUDIO
typedef struct {
	snd_pcm_t* handle;
	snd_pcm_hw_params_t* hw_params;
	snd_pcm_format_t format;
	
	pthread_t thread; //capture thread
	volatile int capturing; //1 if capturing
	unsigned int rate; //sample rate
	unsigned int channel_count; //number of channels capturing
	volatile int dsp_mode; //what should we do with packets captured?
	unsigned int dsp_buffer_sample_count; //buffer size DSP uses to process large ammounts of packets at once (sample count)
	pthread_mutex_t buffer_mutex; //mutex for blocking on shared data (depends on dsp_mode)
	volatile float threshold;  //threshold for dsp_mode 1
	void * capture_dst_buffer; //where processed samples should be stored, can be integer,...
	unsigned int capture_dst_buffer_count; //number of samples stored in the capture_dst_buffer a.t.m.
	unsigned int capture_dst_buffer_size; //size of the dst_buffer (depends on the dsp_mode)
} capture_options;
#endif

typedef struct {
	pthread_t thread; 				     //a thread that will repeat code after client closed connection
	pthread_mutex_t sync_mutex;
	pthread_cond_t sync_cond;		     //send signal here to continue thread
	volatile int waiting_signal;		 //1 if this thread waits for a signal to continue
	volatile int end_current_command;    //1 if we need to end current command
	char *       thread_data;            //holds commands to execute in separate thread
	int			 id;					 //id of thread context
	int          thread_read_index;      //read position 
	int          thread_write_index;     //write position
	int          thread_data_size;       //buffer size
	int			 write_to_thread_buffer; //write to thread buffer with this index
	int			 write_to_own_buffer;	 //1 to write commands to own buffer
	volatile int thread_running;         //becomes 1 there is a thread running, set to 0 to terminate thread
	int			 thread_join_type;		 //determines if we need to wait for thread to exit or just kill it on next thread_start command

	int       command_index;			//current position
	char *    command_line;       		//current command line
	int       command_line_size;		//max bytes in command line
	
	do_loop   loops[MAX_LOOPS];          //positions of 'do' in file loop, max 32 nested loops
	int       loop_index;				 //current loop index

	int		  do_count;					//only used for do ... loop in main thread/file
	int       loop_count;
	int		  execute_main_do_loop;	    //if 1 the start_loop will execute the do command instead of start writing to internal buffer, only in case of the main thread

#ifdef USE_2D
	FT_Library ft_lib;			//needed for .ttf font files in cairo, doc says 1 instance per trhead
	bool ft_init;
#endif

#ifdef USE_AUDIO
	capture_options audio_capture;
#endif 
} thread_context;

thread_context threads[MAX_THREADS+1];

#ifdef USE_2D

typedef struct {
	cairo_surface_t* surface;
	cairo_t* cr;
	cairo_operator_t op; //https://www.cairographics.org/operators/
	int x;
	int y;
	int type;
} cairo_layer;

#define CAIRO_MAX_LAYERS 16
#define LAYER_TYPE_NORMAL 0
#define LAYER_TYPE_CLIP 1
#endif

typedef struct {
	int color_size;//number of color bytes for 1 led, 3 = BGR and 4 = WBGR
	int count;
	int parent_offset;//offset in the parent channel
	int parent_channel_index;//the parent channel (in case of a virtual channel which affects a part of a main channel)
} virtual_channel_t;


typedef struct {
	int channel_type;  //defines if this channel is WS2811 or SK9822
	int channel_index; //the channel number in the *_ledstring, at initialization the channel objects are set
	int color_size;		
	int led_count;     //led count
	bool initialized;  //true if initialized
	ws2811_channel_t* ws2811_channel;
	sk9822_channel_t* sk9822_channel;
	virtual_channel_t* virtual_channel;
	ws2811_led_t* ledstring_1D; //returns 1D led string

#ifdef USE_2D
	int width;
	int height;
	ws2811_led_t*** ledstring_2D; //2D mapped led string ledstring[y][x] returns a pointer to a ws2811_led_t which is the led in the led_string_1D at that location

	unsigned char* surface_data; //binary pixel data 0RGB
	int surface_stride;			 //row size in bytes for each line
	int pixel_stride;			 //number of pixels for each line, = surface_stride / sizeof(int)
	cairo_surface_t* surface;    //painting surface
	cairo_t* cr;			     //current cairo render object, initial this is the main_cr but it can be set to one of the layer cr, all painting functions operate on this cr
	cairo_t* main_cr;			 //cairo main render object

	cairo_surface_t* source_surface;	 //the source surface, can be a loaded PNG, if NULL the source_surface is not used
	cairo_layer layers[CAIRO_MAX_LAYERS]; //layers which are painted first with their operator, only on some functions

#endif
} channel_info;

ws2811_t ws2811_ledstring; //led string object for WS2811 and compatible chips (DATA ONLY)
sk9822_t sk9822_ledstring; //led string object for SK9822 chips (CLOCK + DATA)
channel_info led_channels[MAX_CHANNELS];
pthread_mutex_t ws2812_render_mutex;

void process_character(thread_context * context, char c);

bool file_exists(const char* fname) {
	return access(fname, F_OK) == 0;
}

//handles exit of program with CTRL+C
static void ctrl_c_handler(int signum){
	int i;

	signal(signum, SIG_IGN);

	if (threads[0].thread_running){ //interrupt loop in main thread
		threads[0].end_current_command=1;
		threads[0].thread_running=0;
		signal(SIGINT, ctrl_c_handler);
	}else{ //exit the program
		for (i=0; i<MAX_THREADS+1;i++){
			threads[i].end_current_command=1;
			threads[i].thread_running=0;
		}
		exit_program=1;
		exit(0);
	}
}

static void setup_handlers(void){
	signal(SIGINT, ctrl_c_handler);
    //struct sigaction sa;
    //sa.sa_handler = ctrl_c_handler,
    //sigaction(SIGTERM, &sa, NULL); //SIGKILL
}

//checks if a channel_index of channel_type is free or used
bool is_channel_free(int type, int channel_index) {
	switch (type) {
	case CHANNEL_TYPE_SK9822:
		return sk9822_ledstring.channels[channel_index].count == 0;
		break;
	case CHANNEL_TYPE_WS2811:
		return ws2811_ledstring.channel[channel_index].count == 0;
		break;
	}
}

//returns next free channel index for a channel type
//returns -1 if all used
int get_free_channel_index(int type) {
	int i = 0;
	int count = 0;
	switch (type) {
	case CHANNEL_TYPE_SK9822:
		count = SK9822_MAX_CHANNELS;
		break;
	case CHANNEL_TYPE_WS2811:
		count = RPI_PWM_CHANNELS;
		break;
	}		
	for (i = 0; i < count; i++) {
		if (is_channel_free(type, i)) return i;
	}
	return -1;
}

//returns the number of leds in channel_nr
int get_led_count(int channel_nr) {
	return led_channels[channel_nr].led_count;
}

//returns number of colors for each LED in a channel
int get_color_size(int channel_nr) {
	return led_channels[channel_nr].color_size;
}

//returns led string as led type array, for direct access to leds
ws2811_led_t* get_led_string(int channel_nr) {
	return led_channels[channel_nr].ledstring_1D;
}


#ifdef USE_2D

//returns true if freetype library is initialized
bool init_ft_lib(thread_context* context) {
	if (context->ft_init == false) {
		FT_Error status;

		status = FT_Init_FreeType(& context->ft_lib);
		if (status != 0) {
			fprintf(stderr, "Error %d opening freetype library.\n", status);
			return false;
		} else {
			context->ft_init = true;
		}
		
	}
	return context->ft_init;
}


unsigned int convert_cairo_color(unsigned int color);

ws2811_led_t*** get_2D_led_string(int channel_nr) {
	return led_channels[channel_nr].ledstring_2D;
}

//updates the cairo buffer after changes have been made with 1D functions
//paints what's in the actual led buffers
void cairo_update_buffer(int channel) {

	if (led_channels[channel].surface != NULL) { //if channel 2D surface is not NULL we must update buffer first
		cairo_surface_flush(led_channels[channel].surface);
		unsigned int* pixels = (unsigned int*)cairo_image_surface_get_data(led_channels[channel].surface); //get pointer to pixel data
		ws2811_led_t*** led_string_2D = get_2D_led_string(channel);

		int width = led_channels[channel].width;
		int height = led_channels[channel].height;
		unsigned int stride = (unsigned int)led_channels[channel].pixel_stride;
		for (unsigned int y = 0;y < led_channels[channel].height;y++) { //update all pixels at the corresponding led
			unsigned int* pixel_row = &pixels[y * stride];
			for (unsigned int x = 0;x < led_channels[channel].width;x++) {
				if (led_string_2D[y][x]) *pixel_row = convert_cairo_color( led_string_2D[y][x]->color);
				pixel_row++;
			}
		}
		cairo_surface_mark_dirty(led_channels[channel].surface);
	}
}

//paints all the layer surfaces to the main channel surface
//layer 0 is the bottom
//https://cpp.hotexamples.com/examples/-/-/cairo_set_source_surface/cpp-cairo_set_source_surface-function-examples.html
void cairo_paint_layers(channel_info* chan) {

	cairo_save(chan->main_cr);
	cairo_identity_matrix(chan->main_cr);
	for (int i = 0;i < CAIRO_MAX_LAYERS;i++) {
		if (chan->layers[i].surface != NULL) {
			cairo_set_source_surface(chan->main_cr, chan->layers[i].surface, chan->layers[i].x, chan->layers[i].y);
			//if (chan->layers[i].type == LAYER_TYPE_CLIP) {
			//	cairo_clip(chan->main_cr); //does not work
			//	cairo_paint(chan->main_cr);
			//} else {
				cairo_set_operator(chan->main_cr, chan->layers[i].op);
				cairo_rectangle(chan->main_cr, 0, 0, chan->width, chan->height);
				cairo_fill(chan->main_cr);
			//}
		}
	}
	cairo_restore(chan->main_cr);
}

//resets all layers
void cairo_reset_layers(channel_info* chan) {
	for (int i = 0; i < CAIRO_MAX_LAYERS;i++) {
		if (chan->layers[i].surface != NULL) {
			cairo_surface_destroy(chan->layers[i].surface);
			chan->layers[i].surface = NULL;
		}
		if (chan->layers[i].cr != NULL) {
			cairo_destroy(chan->layers[i].cr);
			chan->layers[i].cr = NULL;
		}
		chan->layers[i].op = CAIRO_OPERATOR_OVER;
		chan->layers[i].type = LAYER_TYPE_NORMAL;
	}
}

//converts x and y to create sharp lines in strokes
//https://www.cairographics.org/FAQ/#sharp_lines.
void cairo_stroke_rounding(double width, double * x1, double * y1) {
	if (((int)width % 2) != 0) {
		*x1 = *x1 + 0.5;
		*y1 = *y1 + 0.5;
	}
}

#endif

//renders a channel
void render_channel(int channel) {

#ifdef USE_2D

	if (led_channels[channel].surface != NULL) { //if channel 2D surface is not NULL we must update buffer first
		cairo_paint_layers(&led_channels[channel]);

		cairo_surface_flush(led_channels[channel].surface);
		unsigned int * pixels = (unsigned int *)cairo_image_surface_get_data(led_channels[channel].surface); //get pointer to pixel data
		ws2811_led_t*** led_string_2D = get_2D_led_string(channel);

		int width = led_channels[channel].width;
		int height = led_channels[channel].height;
		unsigned int stride = (unsigned int)led_channels[channel].pixel_stride;
		for (unsigned int y = 0;y < led_channels[channel].height;y++) { //update all pixels at the corresponding led
			unsigned int * pixel_row = & pixels[y * stride];
			for (unsigned int x = 0;x < led_channels[channel].width;x++) {
				if (led_string_2D[y][x]) {
					led_string_2D[y][x]->color = convert_cairo_color(*pixel_row); // ((led_color << 16) | (led_color >> 16) | (led_color & 0xFF00)) & 0xFFFFFF; 
				}
				pixel_row++;
			}
		}
	}
#endif

	switch (led_channels[channel].channel_type) {
	case CHANNEL_TYPE_SK9822:
		;
		sk9822_return_t sk9822_res = sk9822_render_channel(led_channels[channel].sk9822_channel);
		if (sk9822_res != SK9822_SUCCESS) {
			fprintf(stderr, "sk9822 render failed on channel %d: %s\n", channel, sk9822_get_return_t_str(sk9822_res));
		}
		break;
	case CHANNEL_TYPE_WS2811:
		pthread_mutex_lock(&ws2812_render_mutex);
		ws2811_return_t ws8211_res = ws2811_render(&ws2811_ledstring, channel);
		pthread_mutex_unlock(&ws2812_render_mutex);
		if (ws8211_res != WS2811_SUCCESS) {
			fprintf(stderr, "ws2811 render failed on channel %d: %s\n", channel, ws2811_get_return_t_str(ws8211_res));
		}
		break;
	case CHANNEL_TYPE_VIRTUAL:
		if (led_channels[channel].virtual_channel->parent_channel_index !=-1) {
			render_channel(led_channels[channel].virtual_channel->parent_channel_index);
		}
		break;
	}
}

//returns if channel is a valid led_string index number
int is_valid_channel_number(unsigned int channel) {
	return (channel >= 0) && (channel < MAX_CHANNELS) && led_channels[channel].channel_type!=CHANNEL_TYPE_NONE && led_channels[channel].initialized; 
}

int is_valid_2D_channel_number(unsigned int channel) {
#ifdef USE_2D
	return is_valid_channel_number(channel) && led_channels[channel].surface != NULL;
#else
	return 0;
#endif
}

//writes text to the output depending on the mode (TCP=write to socket)
void write_to_output(char * text){
	switch(mode){
		case MODE_TCP:
			if (active_socket!=-1) {
				if (debug) printf("Sending to client: %s", text);
				write(active_socket,  text, strlen(text));
			}
			break;
		case MODE_FILE:
		case MODE_STDIN:
			printf(text);
	}
}

//allocates memory for command line
void malloc_command_line(thread_context * context, int size){
	if (context->command_line!=NULL) free(context->command_line);
	context->command_line = (char *) malloc(size+1);
	context->command_line_size = size;
	context->command_index = 0;
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

#ifdef USE_2D
//converts cairo pixel to internal LED color OR internal LED color to cairo pixel color
//cairo has different color mapping :( ARGB, led color has ABGR. here we don't use the A
inline unsigned int convert_cairo_color(unsigned int color) {
	return ((((color << 16) | (color >> 16) & 0xFF00FF) | (color & 0xFF00)) & 0xFFFFFF) | 0xFF000000;
}

//converts cairo pixel to internal LED color OR internal LED color to cairo pixel color
//cairo has different color mapping :( ARGB, led color has ABGR.
inline unsigned int convert_cairo_color_rgba(unsigned int color) {
	return (((color << 16) | (color >> 16) & 0xFF00FF) | (color & 0xFF00)) & 0xFFFFFF | (color & 0xFF000000);
}

//sets cairo_source_rgb using internal color GBR
void set_cairo_color_rgb(cairo_t * cr, unsigned int color) {
	unsigned int r, g, b;
	r = color & 0xFF;
	g = (color >> 8) & 0xFF;
	b = (color >> 16) & 0xFF;
	cairo_set_source_rgb(cr, ((double)r) / 255.0, ((double)g) / 255.0, ((double)b) / 255.0);
}

void set_cairo_color_rgba(cairo_t* cr, unsigned int color) {
	unsigned int r, g, b, a;
	r = color & 0xFF;
	g = (color >> 8) & 0xFF;
	b = (color >> 16) & 0xFF;
	a = 255-(color >> 24) & 0xFF;
	cairo_set_source_rgba(cr, ((double)r) / 255.0, ((double)g) / 255.0, ((double)b) / 255.0, ((double)a) / 255.0);
}
#endif
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

//reads key from argument buffer
//example: channel_1_count=10,
//returns channel_1_count in key buffer, then use read_val to read the 10
char * read_key(char * args, char * key, size_t size){
	if (args!=NULL && *args!=0){
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
	}
	return args;
}

//read value from command argument buffer (see read_key)
char * read_val(char * args, char * value, size_t size){
	bool in_str = false;
	bool escaped = false;
	if (args!=NULL && *args!=0){
		size--;
		if (*args==',') args++;
		while (*args!=0 && (*args!=',' || in_str)){
			if (in_str) { //in string, \ acts as an escape character
				if (escaped) { //tells the current character is an escaped character
					if (size > 0) {
						switch (*args) {
						case '"':
							*value = '"';
							break;
						case '\\':
							*value = '\\';
							break;
						case 't':
							*value = '\t';
							break;
						case 'n':
							*value = '\n';
							break;
						default:
							*value = *args;
						}
						value++;
						size--;
					}
					escaped = false;
				} else {
					if (*args == '\\') { //next character is escaped
						escaped = true;
					} else if (*args == '"'){ //end of quoted string
						in_str = false;
					} else if (size > 0) { //add character
						*value = *args;
						value++;
						size--;
					}
				}
			} else {
				if (*args != ' ' && *args != '\t') { //skip space
					if (*args == '"') { //start of string, skip character
						in_str = true;
						escaped = false;
					} else if (size > 0) { //add normal character
						*value = *args;
						value++;
						size--;
					}
				}
			}
			args++;
		}
		*value=0;
	}
	return args;
}

//reads integer from command argument buffer
char * read_int(char * args, int * value){
	char svalue[MAX_VAL_LEN];
	if (args!=NULL && *args!=0){
		args = read_val(args, svalue, MAX_VAL_LEN);
		if (svalue[0]) *value = atoi(svalue);
	}
	return args;
}

char * read_float(char * args, float * value){
	char svalue[MAX_VAL_LEN];
	if (args!=NULL && *args!=0){
		args = read_val(args, svalue, MAX_VAL_LEN);
		if (svalue[0]) *value = atof(svalue);
	}
	return args;
}

char* read_double(char* args, double* value) {
	char svalue[MAX_VAL_LEN];
	if (args != NULL && *args != 0) {
		args = read_val(args, svalue, MAX_VAL_LEN);
		if (svalue[0]) *value = atof(svalue);
	}
	return args;
}

char * read_str(char * args, char * dst, size_t size){
	return read_val(args, dst, size);
}

//reads unsigned integer from command argument buffer
char * read_uint(char * args, unsigned int * value){
	char svalue[MAX_VAL_LEN];
	if (args!=NULL && *args!=0){
		args = read_val(args, svalue, MAX_VAL_LEN);
		if (svalue[0]) *value = (unsigned int) (strtoul(svalue,NULL, 10) & 0xFFFFFFFF);
	}
	return args;	
}

//reads channel number from command line
//automatically adjusts the channel number to 0 based index
char * read_channel(char * args, int * value){
	if (args!=NULL && *args!=0){
		args = read_int(args, value);
		(*value)--;
	}
	return args;
}

//reads color from string, returns string + 6 or 8 characters
//!! args must be hex format only: XXXXXX, USE read_color_arg to read from command line!
//color_size = 3 (RGB format)  or 4 (RGBW format)
char * read_color(char * args, unsigned int * out_color, unsigned int color_size){
    unsigned char r,g,b,w;
    unsigned char color_string[8];
    unsigned int color_string_idx=0;
	if (args!=NULL && *args!=0){
		//*out_color = 0;
		while (*args!=0 && *args!=',' && color_string_idx<color_size*2){
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
			if (color_string_idx) *out_color = color_rgbw(r,g,b,w);
		}else{
			if (color_string_idx) *out_color = color(r,g,b);
		}
	}
    return args;
}

char * read_color_arg(char * args, unsigned int * out_color, unsigned int color_size){
	char value[MAX_VAL_LEN];
	args = read_val(args, value, MAX_VAL_LEN);
	if (*value!=0) read_color(value, out_color, color_size);
	return args;
}

char* read_color_arg_empty(char* args, unsigned int* out_color, unsigned int color_size, bool * arg_empty) {
	char value[MAX_VAL_LEN];
	args = read_val(args, value, MAX_VAL_LEN);
	if (*value != 0) {
		read_color(value, out_color, color_size);
		*arg_empty = false;
	} else {
		*arg_empty = true;
	}
	return args;
}

//reads a hex brightness value
char * read_brightness(char * args, unsigned int * brightness){
    unsigned int idx=0;
    unsigned char str_brightness[2];
	
	if (args!=NULL && *args!=0){
		while (*args!=0 && idx<2){
			if (*args!=' ' && *args!='\t'){ //skip space
				brightness[idx]=*args;
				idx++;
			}
			args++;
		}
		if (idx!=0) * brightness = (hextable[str_brightness[0]] << 4) + hextable[str_brightness[1]];
	}
    return args;
}

#define OP_EQUAL 0
#define OP_OR 1
#define OP_AND 2
#define OP_XOR 3
#define OP_NOT 4

char * read_operation(char * args, char * op){
	char value[MAX_VAL_LEN];
	if (args!=NULL && *args!=0){
		args = read_val(args, value, MAX_VAL_LEN);
		if (strcmp(value, "OR")==0) *op=OP_OR;
		else if (strcmp(value, "AND")==0) *op=OP_AND;
		else if (strcmp(value, "XOR")==0) *op=OP_XOR;
		else if (strcmp(value, "NOT")==0) *op=OP_NOT;
		else if (strcmp(value, "=")==0) *op=OP_EQUAL;
	}	
	return args;
}

//returns time stamp in ms
unsigned long long time_ms(){
	struct timeval tp;
	gettimeofday(&tp, NULL);
	return tp.tv_sec * 1000 + tp.tv_usec / 1000;
}

//expands thread command buffer (increase size by 2 times)
//thread_context is the context that needs memory expansion
void expand_thread_data_buffer(thread_context * context){
	if (context->thread_data_size==0){
		context->thread_data_size=DEFAULT_BUFFER_SIZE;
		context->thread_data = (char *) malloc(context->thread_data_size);
	}else{
		context->thread_data_size = context->thread_data_size * 2;
		char * tmp_buffer = (char *) malloc(context->thread_data_size);
		memcpy((void*) tmp_buffer, (void*)context->thread_data, context->thread_data_size);
		free(context->thread_data);
		context->thread_data = tmp_buffer;
	}
}

//adds data to the thread buffer 
//thread_context is the thread to write to
void write_thread_buffer (thread_context * context, char c){
	if (context->thread_data_size==0) expand_thread_data_buffer(context);
    context->thread_data[context->thread_write_index] = c;
    context->thread_write_index++;
    if (context->thread_write_index==context->thread_data_size) expand_thread_data_buffer(context);
	context->thread_data[context->thread_write_index]=0;
}

//initializes channels
//depending on channel chip type it will initialize the proper hardware for all channels
//init <frequency>,<DMA>
void init_channels(thread_context* context, char* args) {
	int i, parent_index;
	char value[MAX_VAL_LEN];
	int frequency = WS2811_TARGET_FREQ, dma = 10;

	args = read_int(args, &frequency);
	args = read_int(args, &dma);

	bool use_ws2812 = false;
	bool use_sk9822 = false;
	ws2811_return_t ws2811_ret;
	sk9822_return_t sk9822_ret;

	//check which interface to init
	for (i = 0;i < MAX_CHANNELS; i++) {
		led_channels[i].initialized = false;
		switch (led_channels[i].channel_type) {
		case CHANNEL_TYPE_WS2811:
			use_ws2812 = true;
			break;
		case CHANNEL_TYPE_SK9822:
			use_sk9822 = true;
			break;
		}
	}
	
	//init sk9822
	if (use_sk9822) {
		sk9822_fini(&sk9822_ledstring);

		if (debug) printf("Init SPI\n");

		if ((sk9822_ret = sk9822_init(&sk9822_ledstring)) != SK9822_SUCCESS) {
			fprintf(stderr, "sk9822 init SPI failed: %d, %s\n", sk9822_ret, sk9822_get_return_t_str(sk9822_ret));
		}
	}

	//init ws2812
	if (use_ws2812) {
		if (ws2811_ledstring.device != NULL) ws2811_fini(&ws2811_ledstring);
		ws2811_ledstring.dmanum = dma;
		ws2811_ledstring.freq = frequency;

		if (debug) printf("Init ws2811 %d,%d\n", frequency, dma);

		if ((ws2811_ret = ws2811_init(&ws2811_ledstring)) != WS2811_SUCCESS) {
			fprintf(stderr, "ws2811_init failed: %s\n", ws2811_get_return_t_str(ws2811_ret));
		}
	}



	//assign the initialized channels to the global channel array depending on type
	for (i = 0;i < MAX_CHANNELS; i++) {
		led_channels[i].ledstring_1D = NULL;

#ifdef USE_2D
		led_channels[i].width = 0;
		led_channels[i].height = 0;
		if (led_channels[i].ledstring_2D != NULL) {
			for (int k = 0;k < led_channels[i].height;k++) {
				free(led_channels[i].ledstring_2D[k]);
			}
			free(led_channels[i].ledstring_2D);
		}

		/*for (int k = 0;k < CAIRO_MAX_LAYERS;k++) {
			if (led_channels[i].layers[k].cr != NULL) cairo_destroy(led_channels[i].layers[k].cr);
			if (led_channels[i].layers[k].surface != NULL) cairo_surface_destroy(led_channels[i].layers[k].surface);
			led_channels[i].layers[k].x = 0;
			led_channels[i].layers[k].y = 0;
		}*/

		if (led_channels[i].main_cr !=NULL) cairo_destroy(led_channels[i].main_cr);
		if (led_channels[i].surface != NULL) cairo_surface_destroy(led_channels[i].surface);
		if (led_channels[i].surface_data != NULL) free(led_channels[i].surface_data);
#endif 

		if (led_channels[i].channel_type != CHANNEL_TYPE_NONE) {
			switch (led_channels[i].channel_type) {
			case CHANNEL_TYPE_WS2811:
				led_channels[i].initialized = ws2811_ret == WS2811_SUCCESS;
				led_channels[i].ws2811_channel = &ws2811_ledstring.channel[led_channels[i].channel_index];
				led_channels[i].ledstring_1D = led_channels[i].ws2811_channel->leds;
				if (debug) printf("Init WS2811 for channel: %d, res=%d.\n", i, led_channels[i].initialized ? 1 : 0);
				break;
			case CHANNEL_TYPE_SK9822:
				led_channels[i].initialized = sk9822_ret == SK9822_SUCCESS;
				led_channels[i].sk9822_channel = &sk9822_ledstring.channels[led_channels[i].channel_index];
				led_channels[i].ledstring_1D = (ws2811_led_t*)led_channels[i].sk9822_channel->leds;
				if (debug) printf("Init SK9822 for channel: %d, res=%d.\n", i, led_channels[i].initialized ? 1 : 0);
				break;
			case CHANNEL_TYPE_VIRTUAL:
				parent_index = led_channels[i].virtual_channel->parent_channel_index;
				led_channels[i].initialized = led_channels[parent_index].initialized;
				led_channels[i].ledstring_1D = get_led_string(parent_index) + led_channels[i].virtual_channel->parent_offset;

				if (debug) printf("Init virtual channel: %d, parent channel %d, offset %d.\n", i, parent_index, led_channels[i].virtual_channel->parent_offset);
				break;
			}
#ifdef USE_2D
			led_channels[i].ledstring_2D = NULL;
			led_channels[i].surface = NULL;
			led_channels[i].surface_data = NULL;
			led_channels[i].cr = NULL;
			led_channels[i].main_cr = NULL;
#endif
			//initialize the default brightness of each led
			ws2811_led_t* leds = get_led_string(i);
			if (leds != NULL) {
				int count = get_led_count(i);
				for (int j = 0;j < count ;j++) {
					leds[j].brightness = 0xFF;
				}
			}
		}
	}
}

#ifdef USE_2D
//initialize 2D channel
//config_2D <channel>,<width>,<height>,<panel_type>,<panel_size_x>,<panel_size_y>,<start_led>,<map_file>
void config_2D(thread_context* context, char* args) {
	int channel = 0;
	int width = 0, height = 0, panel_type = 0, panel_size_x = 0, panel_size_y = 0, len=0, start_led=0;
	char filename[MAX_VAL_LEN];

	if (is_valid_channel_number(channel)) {
		len = get_led_count(channel);
	}

	args = read_channel(args, &channel);
	args = read_int(args, &width);
	args = read_int(args, &height);
	args = read_int(args, &panel_type);
	args = read_int(args, &panel_size_x);
	args = read_int(args, &panel_size_y);
	args = read_int(args, &start_led);
	args = read_str(args, filename, sizeof(filename));

	if (is_valid_channel_number(channel)) {
		len = get_led_count(channel);

		if (len < width * height + start_led) {
			fprintf(stderr, "Error: width * height + start_led must be at least the length of the led string.\n");
			return;
		}

		if (panel_size_x == 0) panel_size_x = width;
		if (panel_size_y == 0) panel_size_y = height;

		if ((width % panel_size_x) != 0) {
			fprintf(stderr, "Error: width must be a multiple of panel_size_x.\n");
			return;
		}

		if ((height % panel_size_y) != 0) {
			fprintf(stderr, "Error: height must be multiple of panel_size_y.\n");
			return;
		}

		int panels_x = width / panel_size_x;
		int panels_y = height / panel_size_y;

		channel_info* led_channel = &led_channels[channel];
		ws2811_led_t*** ledstring_2D;
		ws2811_led_t* ledstring_1D;

		if (led_channel->ledstring_2D != NULL) {
			for (int k = 0;k < led_channel->height;k++) {
				free(led_channel->ledstring_2D[k]);
			}
			free(led_channel->ledstring_2D);
		}

		if (led_channel->cr != NULL) cairo_destroy(led_channel->cr);
		if (led_channel->surface != NULL) cairo_surface_destroy(led_channel->surface);
		if (led_channel->surface_data != NULL) free(led_channel->surface_data);

		led_channel->width = width;
		led_channel->height = height;
		ledstring_1D = get_led_string(channel);
		ledstring_1D += start_led; //move to starting position
		ledstring_2D = (ws2811_led_t***)malloc(height * sizeof(ws2811_led_t**));

		for (int i = 0; i < height;i++) {
			ledstring_2D[i] = (ws2811_led_t**)malloc(width * sizeof(ws2811_led_t*));
			memset(ledstring_2D[i], 0, width * sizeof(ws2811_led_t*));
		}

		led_channel->ledstring_2D = ledstring_2D;
		
		int x, y, offset;

		switch (panel_type) {
		case 0: //normal type, row by row
			offset = 0;
			for (y = 0;y < height;y++) {
				for (x = 0;x < width; x++) {
					ledstring_2D[y][x] = &ledstring_1D[offset];
					offset++;
				}
			}
			break;
		case 1: //normal type, row by row but odd rows are reverse connected meaning leds connected from right to left, display starts at left top
			offset = 0;
			for (y = 0;y < height;y++) {
				for (x = 0;x < width; x++) {
					if ((y % 2) == 0) {
						ledstring_2D[y][x] = &ledstring_1D[offset];
					} else {
						ledstring_2D[y][width - x - 1] = &ledstring_1D[offset];
					}
					offset++;
				}
			}
			break;
		case 2: //panels with vertical rows for each panel starting left top of each panel row
			for (y = 0;y < height;y++) {
				for (x = 0;x < width; x++) {
					offset = (y / panel_size_y) * panel_size_y * width;
					offset += x * panel_size_y;
					if ((x & 1) != 0) offset += (panel_size_y - (y % panel_size_y) - 1);
					else offset += (y % panel_size_y);
					ledstring_2D[y][x] = &ledstring_1D[offset];
				}
			}
			break;
		case 3: //panels with individual vertical rows starting left top of each panel row but odd panel rows start from the right bottom.
			for (y = 0;y < height;y++) {
				for (x = 0;x < width; x++) {
					int panel_row_index = y / panel_size_y;
					offset = panel_row_index * panel_size_y * width;
					if ((panel_row_index % 2) == 0) { //even panel row same as in case 1
						offset += x * panel_size_y;
						if ((x & 1) != 0) offset += (panel_size_y - (y % panel_size_y) - 1);
						else offset += (y % panel_size_y);
						ledstring_2D[y][x] = &ledstring_1D[offset];
					} else { //odd panel rows, start from the right top, complicated but easy to connect multiple panels :) ...
						offset += (width  - x - 1) * panel_size_y;
						if ((x & 1) == 0) offset += (y % panel_size_y);
						else offset += (panel_size_y - (y % panel_size_y) - 1);
						ledstring_2D[y][x] = &ledstring_1D[offset];
					}
				}
			}
			break;
		case 4: //custom map from file, file format:  X, Y = LED_INDEX \n
			;
			FILE * file = fopen(filename, "r");

			if (debug) printf("Reading led map file %s\n", filename);
			if (file == NULL) {
				perror("Error loading map file ");
				return;
			}

			x = 0;
			y = 0;
			char line[1024];
			while (!feof(file)) {
				int offset = 0;
				fscanf(file, "%d %d %d", &x, &y, &offset);
				if ((x >= 0 && x < width) && (y >= 0 && y < height)) {
					ledstring_2D[y][x] = &ledstring_1D[offset];
				}
			}
			
			fclose(file);
			break;
		default:
			fprintf(stderr, "Error invalid panel_type\n");

			break;
		}
		
		led_channel->surface_stride = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, width);
		led_channel->pixel_stride = led_channel->surface_stride / sizeof(unsigned int);

		led_channel->surface_data = (unsigned char*)malloc(led_channel->surface_stride * height * sizeof(unsigned char));

		led_channel->surface =  cairo_image_surface_create_for_data(led_channel->surface_data, CAIRO_FORMAT_RGB24, width , height, led_channel->surface_stride);

		led_channel->main_cr = cairo_create(led_channel->surface);
		led_channel->cr = led_channel->main_cr;
		
		cairo_set_source_rgba(led_channel->cr, 0, 0, 0, 0);
		//cairo_pattern_set_filter(cairo_get_source(led_channel->cr), CAIRO_FILTER_NEAREST);
		cairo_set_operator(led_channel->cr, CAIRO_OPERATOR_SOURCE);
		cairo_paint(led_channel->cr);
		//cairo_set_antialias(led_channel->cr, CAIRO_ANTIALIAS_NONE);

	}else {
		fprintf(stderr, ERROR_INVALID_CHANNEL);
	}
}
#endif

//resets all initialized channels and sets memory
void reset_led_strings() {
	int i = 0;

	if (debug) printf("Resetting all channels.\n");

	if (ws2811_ledstring.device != NULL) ws2811_fini(&ws2811_ledstring);
	sk9822_fini(&sk9822_ledstring);

	ws2811_ledstring.device = NULL;
	for (i = 0;i < RPI_PWM_CHANNELS;i++) {
		ws2811_ledstring.channel[0].gpionum = 0;
		ws2811_ledstring.channel[0].count = 0;
		ws2811_ledstring.channel[1].gpionum = 0;
		ws2811_ledstring.channel[1].count = 0;
	}

	for (i = 0; i < MAX_CHANNELS; i++) {
		led_channels[i].led_count = 0;
		led_channels[i].color_size = 0;
		led_channels[i].channel_index = -1;
		led_channels[i].channel_type = CHANNEL_TYPE_NONE;
		led_channels[i].initialized = false;
		led_channels[i].sk9822_channel = NULL;
		led_channels[i].ws2811_channel = NULL;
		led_channels[i].ledstring_1D = NULL;

		if (led_channels[i].virtual_channel != NULL) {
			free(led_channels[i].virtual_channel);
		}
		led_channels[i].virtual_channel = NULL;

#ifdef USE_2D
		if (led_channels[i].ledstring_2D != NULL) {
			for (int k = 0;k < led_channels[i].height;k++) {
				free(led_channels[i].ledstring_2D[k]);
			}
			free(led_channels[i].ledstring_2D);
			led_channels[i].ledstring_2D = NULL;
		}


		if (led_channels[i].cr != NULL) cairo_destroy(led_channels[i].cr);
		if (led_channels[i].surface != NULL) cairo_surface_destroy(led_channels[i].surface);
		if (led_channels[i].surface_data != NULL) free(led_channels[i].surface_data);

		led_channels[i].surface = NULL;
		led_channels[i].surface_data = NULL;
		cairo_reset_layers(&led_channels[i]);
#endif
	}

	create_sk9822(&sk9822_ledstring);

}

//sets the ws2811 channels
//setup channel, led_count, type, invert, global_brightness, GPIO
//setup channel, led_count, type, invert, global_brightness, SPI_DEV, SPI_SPEED, ALT_SPI_PIN
//setup channel, led_count, type, parent_channel_number, offset
//type: WS2811_STRIP* OR SK9822_STRIP* OR SK6812_STRIP OR CHANNEL_TYPE_VIRTUAL
void setup_ledstring(thread_context * context, char * args){
    int channel=0, led_count=10, type=0, invert=0, brightness=255, GPIO=18, spi_speed = SK9822_DEFAULT_SPI_SPEED, alt_spi_pin= SK9822_DEFAULT_SPI_GPIO, parent_channel_index=-1, offset=0;
	char spi_dev[PATH_MAX];

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
                           SK6812_STRIP_BGRW,//11

						   SK9822_STRIP_RGB, //12
						   SK9822_STRIP_RBG, //13
						   SK9822_STRIP_GRB, //14
						   SK9822_STRIP_GBR, //15
						   SK9822_STRIP_BRG, //16
						   SK9822_STRIP_BGR  //17

                           };
    
	args = read_channel(args, & channel);
	args = read_int(args, & led_count);
	args = read_int(args, & type);

    
	int free_channel_index = -1;

	if (channel >= 0 && channel < MAX_CHANNELS) {

		if (debug) printf("Initialize channel %d,%d,%d,%d,%d,%d.\n", channel, led_count, type, invert, brightness, GPIO);

		//check if the given channel is already in use
		if (is_valid_channel_number(channel)) {
			fprintf(stderr, "Error Channel %d is already in use, use reset command to reinitialize all channels.\n", channel + 1);
			return;
		}

		int channel_type = CHANNEL_TYPE_NONE;
		int color_size = 4;

		if (type != CHANNEL_TYPE_VIRTUAL) {
			
			args = read_int(args, &invert);
			args = read_int(args, &brightness);

			switch (led_types[type]) {
			case WS2811_STRIP_RGB:
			case WS2811_STRIP_RBG:
			case WS2811_STRIP_GRB:
			case WS2811_STRIP_GBR:
			case WS2811_STRIP_BRG:
			case WS2811_STRIP_BGR:
				channel_type = CHANNEL_TYPE_WS2811;
				color_size = 3;
				if (type > 11) channel_type = CHANNEL_TYPE_SK9822;
				break;
			case SK6812_STRIP_RGBW:
			case SK6812_STRIP_RBGW:
			case SK6812_STRIP_GRBW:
			case SK6812_STRIP_GBRW:
			case SK6812_STRIP_BRGW:
			case SK6812_STRIP_BGRW:
				channel_type = CHANNEL_TYPE_WS2811;
				color_size = 4;
				break;
			}

			free_channel_index = get_free_channel_index(channel_type); //get next free channel for WS2811 or SK9822 depending on what user wants
			led_channels[channel].channel_index = free_channel_index; //save this for the init procedure
		} else {
			channel_type = CHANNEL_TYPE_VIRTUAL;
		}

		led_channels[channel].channel_type = channel_type; //save the type of channel

		switch (channel_type) {
		case CHANNEL_TYPE_WS2811:
			if (free_channel_index == -1) {
				fprintf(stderr, "Error no free channels anymore for led type WS2811/SK6812, max number of PWM led strings is %d\n", RPI_PWM_CHANNELS);
				return;
			}

			if (debug) printf("Creating WS2812 channel at index %d.\n", free_channel_index);

			args = read_int(args, &GPIO);

			ws2811_ledstring.channel[free_channel_index].gpionum = GPIO;
			ws2811_ledstring.channel[free_channel_index].invert = invert;
			ws2811_ledstring.channel[free_channel_index].count = led_count;
			ws2811_ledstring.channel[free_channel_index].strip_type = led_types[type];
			ws2811_ledstring.channel[free_channel_index].brightness = brightness;
			ws2811_ledstring.channel[free_channel_index].color_size = color_size;
			led_channels[channel].led_count = led_count;
			led_channels[channel].color_size = color_size;
			break;
		case CHANNEL_TYPE_SK9822:
			if (free_channel_index == -1) {
				fprintf(stderr, "Error no free channels anymore for led type SK9822, max number of SPI led strings is %d\n", SK9822_MAX_CHANNELS);
				return;
			}

			if (debug) printf("Creating SK9822 channel at index %d.\n", free_channel_index);

			strcpy(spi_dev, "/dev/spidev0.0");
			GPIO = SK9822_DEFAULT_SPI_GPIO;

			args = read_str(args, spi_dev, sizeof(spi_dev));
			args = read_int(args, &spi_speed);
			args = read_int(args, &GPIO);

			sk9822_ledstring.channels[free_channel_index].brightness = brightness;
			sk9822_ledstring.channels[free_channel_index].color_size = color_size;
			sk9822_ledstring.channels[free_channel_index].count = led_count;
			sk9822_ledstring.channels[free_channel_index].gpionum = GPIO;
			sk9822_ledstring.channels[free_channel_index].invert = invert;
			strcpy(sk9822_ledstring.channels[free_channel_index].spi_dev, spi_dev);
			sk9822_ledstring.channels[free_channel_index].spi_speed = spi_speed;
			sk9822_ledstring.channels[free_channel_index].strip_type = led_types[type];
			led_channels[channel].led_count = led_count;
			led_channels[channel].color_size = color_size;
			break;
		case CHANNEL_TYPE_VIRTUAL:
			args = read_channel(args, &parent_channel_index);
			args = read_int(args, &offset);

			if (parent_channel_index >=0 && parent_channel_index < MAX_CHANNELS && led_channels[parent_channel_index].channel_type!=CHANNEL_TYPE_NONE) {
				int max_led_count = led_count;
				if (max_led_count > get_led_count(parent_channel_index) - offset) max_led_count = get_led_count(parent_channel_index) - offset;
				if (max_led_count <= 0) {
					fprintf(stderr, "Invalid offset / led count exeeds number of leds in parent string.\n");
					return;
				}

				if (led_channels[channel].virtual_channel == NULL) free(led_channels[channel].virtual_channel);

				led_channels[channel].virtual_channel = (virtual_channel_t*)malloc(sizeof(virtual_channel_t));
				led_channels[channel].channel_index = -1; //save this for the init procedure
				led_channels[channel].virtual_channel->color_size = led_channels[parent_channel_index].color_size;
				led_channels[channel].virtual_channel->count = max_led_count;
				led_channels[channel].virtual_channel->parent_offset = offset;
				led_channels[channel].led_count = max_led_count;
				led_channels[channel].color_size = led_channels[parent_channel_index].color_size;
			} else {
				fprintf(stderr, "Invalid parent channel number %d\n", parent_channel_index + 1);
				return;
			}
			break;
		default:
			fprintf(stderr, "Error Unknown led type for setup command of channel %d, led type: %d\n", channel + 1, type);
			return;
		}
		
        int max_size=0,i;
        for (i=0; i<RPI_PWM_CHANNELS;i++){
            int size = DEFAULT_COMMAND_LINE_SIZE + led_count  * 2 * color_size;
            if (size > max_size){
                max_size = size;
            }
        }
        malloc_command_line(context, max_size); //allocate memory for full render data    
    }else{
        if (debug) printf("Channel number %d\n", channel);
        fprintf(stderr,"Invalid channel number, must be 1-%d\n", MAX_CHANNELS);
    }
}

//changes the global channel brightness
//global_brightness <channel>,<value>
void global_brightness(thread_context* context, char* args) {
	int channel = 1, brightness = 255;
	args = read_channel(args, &channel);
	args = read_int(args, &brightness);
	if (is_valid_channel_number(channel)) {
		switch (led_channels[channel].channel_type) {
		case CHANNEL_TYPE_WS2811:
			led_channels[channel].ws2811_channel->brightness = brightness;
			break;
		case CHANNEL_TYPE_SK9822:
			led_channels[channel].sk9822_channel->brightness = brightness;
			break;
		}
		if (debug) printf("Global brightness %d, %d\n", channel, brightness);
	}
	else {
		fprintf(stderr, ERROR_INVALID_CHANNEL);
	}
}


//prints channel settings
void print_settings(){
    unsigned int i;
	bool ws2811_used = false;
	for (i = 0;i < MAX_CHANNELS;i++) {
		if (led_channels[i].channel_type != CHANNEL_TYPE_NONE) {
			printf("Channel %d:\n", i + 1);
			printf("    Initialized: %d\n", led_channels[i].initialized);
			printf("    driver channel: %d\n", led_channels[i].channel_index);
			switch (led_channels[i].channel_type) {
			case CHANNEL_TYPE_WS2811:
				ws2811_used = true;
				printf("    Type:   WS2811\n");
				printf("    GPIO:   %d\n", ws2811_ledstring.channel[i].gpionum);
				printf("    Invert: %d\n", ws2811_ledstring.channel[i].invert);
				printf("    Count:  %d\n", ws2811_ledstring.channel[i].count);
				printf("    Colors: %d\n", ws2811_ledstring.channel[i].color_size);
				printf("    Type:   %d\n", ws2811_ledstring.channel[i].strip_type);
				break;
			case CHANNEL_TYPE_SK9822:
				printf("    Type:        SK9822\n");
				printf("    SPI-DEV:     %s\n", sk9822_ledstring.channels[i].spi_dev);
				printf("    SPI-Speed:   %d kHz\n", sk9822_ledstring.channels[i].spi_speed / 1000);
				printf("    GPIO:        %d\n", sk9822_ledstring.channels[i].gpionum);
				break;
			case CHANNEL_TYPE_VIRTUAL:
				printf("	Type:		Virtual\n");
				printf("	Parent:		%d\n", led_channels[i].virtual_channel->parent_channel_index);
				printf("	Offset:		%d\n", led_channels[i].virtual_channel->parent_offset);
				printf("	Count:		%d\n", led_channels[i].virtual_channel->count);
				printf("	Colors:		%d\n", led_channels[i].virtual_channel->color_size);
			}
		}
	}
	if (ws2811_used) {
		printf("WS2812 DMA Freq:     %d\n", ws2811_ledstring.freq);
		printf("WS2812 DMA Num:      %d\n", ws2811_ledstring.dmanum);
		//printf("WS2811 driver mode:  %d\n", ws2811_ledstring.device.driver_mode);
	}
}

//sends the buffer to the leds
//render <channel>,0,AABBCCDDEEFF...
//optional the colors for leds:
//AABBCC are RGB colors for first led
//DDEEFF is RGB for second led,...
void render(thread_context * context, char * args){
	int channel=0;
	int r,g,b,w;
	int size;
    int start;
    char color_string[6];
    
	if (debug) printf("Render %s\n", args);
	
    if (args!=NULL){
		args = read_channel(args, & channel); //read_val(args, & channel, MAX_VAL_LEN);
		//channel = channel-1;
        if (is_valid_channel_number(channel)){
            if (*args!=0){
                args = read_int(args, & start); //read start position
                while (*args!=0 && (*args==' ' || *args==',')) args++; //skip white space
                
                if (debug) printf("Render channel %d selected start at %d leds %d\n", channel, start, get_led_count(channel));
                
                size = strlen(args);
                int led_count = get_led_count(channel);            
                int led_index = start % led_count;
                int color_count = get_color_size(channel);
				ws2811_led_t* leds = get_led_string(channel);

                while (*args!=0){
                    unsigned int color=0;
                    args = read_color(args, & color, color_count);
                    leds[led_index].color = color;
                    led_index++;
                    if (led_index>=led_count) led_index=0;
                }
            }			
        }
	}
	
	if (is_valid_channel_number(channel)){
		render_channel(channel);
	}else{
		fprintf(stderr,ERROR_INVALID_CHANNEL);
	}
}

//include all effects
#include "effects/rotate.c"
#include "effects/rainbow.c"
#include "effects/fill.c"
#include "effects/brightness.c"
#include "effects/fade.c"
#include "effects/blink.c"
#include "effects/gradient.c"
#include "effects/add_random.c"
#include "effects/random_fade_in_out.c"
#include "effects/chaser.c"
#include "effects/color_change.c"
#include "effects/fly_out.c"
#include "effects/fly_in.c"
#include "effects/progress.c"


#ifdef USE_JPEG
	#include "effects/read_jpg.c"
#endif


#ifdef USE_PNG
	#include "effects/read_png.c"
#endif

#ifdef USE_2D
	#include "2D/set_pixel.c"
	#include "2D/screenshot.c"
	#include "2D/print_text.c"
	#include "2D/cls.c"
	#include "2D/message_board.c"
	#include "2D/rectangle.c"
	#include "2D/line.c"
	#include "2D/circle.c"
	#include "2D/image.c"
	#include "2D/init_layer.c"
	#include "2D/change_layer.c"
	#include "2D/text_input.c"
#endif

#ifdef USE_AUDIO
	#include "audio/record.c"
	#include "audio/pulses.c"
	#include "audio/light_organ.c"
	#include "audio/vu_meter.c"
#endif

//save_state <channel>,<filename>,<start>,<len>
void save_state(thread_context * context, char * args){
	int channel=0,start=0, len=0, color, brightness,i=0;
	char filename[MAX_VAL_LEN];

	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)) len=get_led_count(channel);
	args = read_str(args, filename, sizeof(filename));
	args = read_int(args, & start);
	args = read_int(args, & len);
	
	if (is_valid_channel_number(channel)){		
		FILE * outfile;

		if (start<0) start=0;
        if (start+len> get_led_count(channel)) len = get_led_count(channel)-start;
		
		if (debug) printf("save_state %d,%s,%d,%d\n", channel, filename, start, len);
		
		if ((outfile = fopen(filename, "wb")) == NULL) {
			fprintf(stderr, "Error: can't open %s\n", filename);
			return;
		}
		ws2811_led_t * leds = get_led_string(channel);
		
		for (i=0;i<len;i++){
			color = leds[start+i].color;
			brightness = leds[start+i].brightness;
			
			fprintf(outfile,"%08X,%02X\n", color, brightness);
		}
		fclose(outfile);
	}
}

//load_state <channel>,<filename>,<start>,<len>
void load_state(thread_context * context, char * args){
	FILE * infile;		/* source file */
	int channel=0,start=0, len=0, color, brightness, i=0;
	char filename[MAX_VAL_LEN];
	char fline[MAX_VAL_LEN];

	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)) len=get_led_count(channel);
	args = read_str(args, filename, sizeof(filename));
	args = read_int(args, & start);
	args = read_int(args, & len);
	
	if (is_valid_channel_number(channel)){	
	
		if (start<0) start=0;
		if (start+len> get_led_count(channel)) len = get_led_count(channel)-start;
		
		if (debug) printf("load_state %d,%s,%d,%d\n", channel, filename, start, len);
		
		if ((infile = fopen(filename, "rb")) == NULL) {
			fprintf(stderr, "Error: can't open %s\n", filename);
			return;
		}
		
		ws2811_led_t* leds = get_led_string(channel);
		
		while (i < len && !feof(infile) && fscanf(infile, "%x,%x", & color, & brightness)>0){
			leds[start+i].color = color;
			leds[start+i].brightness=brightness;
			if (debug) printf("load_state set color %d,%d,%d\n", start+i, color, brightness);
			i++;
		}
		fclose(infile);
	}
}

//do ...
void start_loop (thread_context * context, char * args){
	int i;
	if (context->id>0 || context->execute_main_do_loop!=0){
        if (context->loop_index<MAX_LOOPS){
			int start = 0; int add_to_index = 0;
			args = read_int(args, &start);
			args = read_int(args, &add_to_index);
            if (debug) printf ("do %d\n", context->thread_read_index);
            context->loops[context->loop_index].do_pos = context->thread_read_index-1;
            context->loops[context->loop_index].n_loops=start;
			context->loops[context->loop_index].add_to_index = add_to_index;
            context->loop_index++;
        }else{
            printf("Warning max nested loops reached in thread %d!\n", context->id);
        }	
	}else{
		if (debug) printf ("start do..loop in main thread.\n");
		context->write_to_own_buffer=1;
        context->thread_write_index=0;
		context->do_count=1;
		context->loop_count=0;
		write_thread_buffer(context, 'd');
		write_thread_buffer(context, 'o');
		if (args!=NULL && strlen(args)!=0){
			write_thread_buffer(context, ' ');
			for (i=0;i<strlen(args); i++){
				write_thread_buffer(context, args[i]);
			}
		}
		write_thread_buffer(context, ';');
	}
}

//ends a do ... loop
//<nr_loops> max loop counter value, default endless loop
//<step> increase loop counter with this value every interation, default 1
void end_loop(thread_context * context, char * args){
    int max_loops = 0; //number of wanted loops
	int step = 1;
    if (args!=NULL){
		args = read_int(args, &max_loops);
		args = read_int(args, &step);
    }

	if (debug) printf("loop %d,%d,%d,%d\n", context->thread_read_index, context->loop_index, max_loops, step);

	if (context->loop_index>0){
		context->loops[context->loop_index-1].n_loops+=step;
		if (max_loops==0 || context->loops[context->loop_index-1].n_loops<max_loops){ //if number of loops is 0 = loop forever
			context->thread_read_index = context->loops[context->loop_index-1].do_pos;
		}else{
			if (context->loop_index>0) context->loop_index--; //exit loop
		}   
	}
}


//sets join type for next socket connect if thread is active
// set_thread_exit_type_type <thread_index>,<join_type>
//<thread_index> = 0
//<join_type> 0 -> Cancel, 1 -> wait
void set_thread_exit_type(thread_context * context,char * args){
	
	int thread_index = 0;
	int join_type = JOIN_THREAD_CANCEL;
	
	args = read_int(args, & thread_index);
	args = read_int(args, & join_type);
	
	if (join_type==JOIN_THREAD_CANCEL || join_type==JOIN_THREAD_WAIT){
		
		context->thread_join_type=join_type;
	}else{
		fprintf(stderr, "Invalid join type %d\n", join_type);
	}
	
}

//initializes the memory for a TCP/IP multithread buffer
//thread_start <index>,<join_type>
void init_thread(thread_context * context, char * args){
	int thread_index = 1;
	int join_thread_type = -1;

	args = read_int(args, & thread_index);
	args = read_int(args, & join_thread_type);

	if (thread_index > 0 && thread_index <= MAX_THREADS){
		thread_context	* t_context = & threads[thread_index];

		if (join_thread_type!=-1){
			t_context->thread_join_type = join_thread_type;
		}
		if (t_context->thread_running){ //a thread is already running
			switch (t_context->thread_join_type){
				case JOIN_THREAD_WAIT:
					//do nothing special but wait for thread to end
					break;
				default: //default is cancel
					t_context->end_current_command=1; //end current command
					t_context->thread_running=0; //exit the thread
					break;
			}
			if (debug) printf("Joining thread %d.\n", t_context->id);
			if (t_context->waiting_signal) pthread_cond_signal (&t_context->sync_cond);
		}
		if (t_context->thread!=0){
			int res = pthread_join(t_context->thread,NULL); //wait for thread to finish, clean up and exit
			if (res!=0){
				fprintf(stderr,"Error join thread: %d, %d ", t_context->id, res);
				perror(NULL);
			}
			t_context->thread=0;
		}
		write_to_output("READY\r\n"); //send back command ready to accept incoming commands
		t_context->end_current_command=0;

		if (t_context->thread_data==NULL){
			t_context->thread_data = (char *) malloc(DEFAULT_BUFFER_SIZE);
			t_context->thread_data_size = DEFAULT_BUFFER_SIZE;
		}
		if (t_context->command_line==NULL) malloc_command_line(t_context, DEFAULT_COMMAND_LINE_SIZE > threads[0].command_line_size ? DEFAULT_COMMAND_LINE_SIZE : threads[0].command_line_size);
		t_context->thread_write_index=0;
		context->write_to_thread_buffer=thread_index; //from now we save all commands to the thread buffer
	}else{
		fprintf(stderr, "Invalid thread number must be in range: 1-%d.", MAX_THREADS);
	}
}

//this function is called by p_thread when a new thread starts, the parameter is a pointer to the thread_context holding commands to execute
void thread_func (thread_context * context){
    context->thread_read_index=0;
	context->command_index=0;
	context->command_line[0]=0;
    if (debug) printf("Enter thread %d,%d,%d,%d.\n", context->thread_running, context->thread_read_index, context->thread_write_index, context->id);

    while (context->thread_running){
        char c = context->thread_data[context->thread_read_index];
        process_character(context, c);
        context->thread_read_index++;
        if (context->thread_read_index>=context->thread_write_index) break; //exit loop if we are at the end of the file
		//to do: check in TCP mode if client disconnected and terminate endless loops
		//if (context->id==0 && mode==MODE_TCP && )
    }
    context->thread_running=0;
    if (debug) printf("Exit thread %d.\n", context->id);
    if (context->id!=0) pthread_exit(NULL); //exit the tread
}

//starts a new thread, given the index where context is located
void start_thread(int thread_context_index){
    if (debug) printf("Creating pthread %d.\n", thread_context_index);
	thread_context * context = &threads[thread_context_index];

	context->thread_running=1;
    int s = pthread_create(& context->thread, NULL, (void* (*)(void*)) & thread_func, context);
	if (s!=0){
		fprintf(stderr,"Error creating new thread: %d", s);
		perror(NULL);
	}

}

//wait for a thread to finish
//wait_thread <id>
void wait_thread(thread_context * context, char * args){
	int thread_index = 1;

	args = read_int(args, & thread_index);
	if (thread_index > 0 && thread_index <= MAX_THREADS){
		thread_context	* t_context = & threads[thread_index];
		if (t_context->thread_running){
			if (debug) printf("Waiting for thread %d to finish.\n", thread_index);
			int res = pthread_join(t_context->thread,NULL); //wait for thread to finish, clean up and exit
			if (res!=0){
				fprintf(stderr,"Error join thread: %d, %d ", t_context->id, res);
				perror(NULL);
			}
			t_context->thread=0;
			if (debug) printf("Thread %d finished.", thread_index);
		}
	}else{
		fprintf(stderr, "Invalid thread id %d", thread_index);
	}
}


//terminate a running thread
//kill_thread <id>, <join_type>
void kill_thread(thread_context * context, char * args){
	int thread_index = 1;
	int join_thread_type = -1;

	args = read_int(args, & thread_index);
	args = read_int(args, & join_thread_type);

	if (thread_index > 0 && thread_index <= MAX_THREADS){
		thread_context	* t_context = & threads[thread_index];

		if (join_thread_type!=-1){
			t_context->thread_join_type = join_thread_type;
		}

		if (t_context->thread_running){ //a thread is already running
			if (debug) printf("Killing thread %d,%d.\n", thread_index, join_thread_type);
			switch (t_context->thread_join_type){
				case JOIN_THREAD_WAIT:
					//do nothing special but wait for thread to end
					break;
				default: //default is cancel
					t_context->end_current_command=1; //end current command
					t_context->thread_running=0; //exit the thread
					break;
			}
			if (debug) printf("Joining thread %d.\n", t_context->id);
			if (t_context->waiting_signal) pthread_cond_signal (&t_context->sync_cond);
			int res = pthread_join(t_context->thread,NULL); //wait for thread to finish, clean up and exit
			if (res!=0){
				fprintf(stderr,"Error join thread: %d, %d ", t_context->id, res);
				perror(NULL);
			}
			t_context->thread=0;
			t_context->end_current_command=0;
		}else{
			if (debug) printf("Thread %d not running.\n", thread_index);
		}
	}else{
		fprintf(stderr, "Invalid thread id %d\n", thread_index);
	}
}

//sends a signal to a thread and optionaly waits for the thread to reach a <wait_signal> command to ensure 2 way synchronization
//signal_thread <thread index>, <wait until thread is waiting for the signal>
void signal_thread(thread_context * context, char * args){
	int thread_index = 1;
	int wait_for_thread = 0;
	args = read_int(args, & thread_index);
	args = read_int(args, & wait_for_thread);

	if (thread_index > 0 && thread_index <= MAX_THREADS){
		thread_context	* t_context = & threads[thread_index];
		if (debug) printf("Sending signal to thread %d from %d.\n", thread_index, context->id);
		while (wait_for_thread && !t_context->waiting_signal) usleep(1000); //if needed wait for the thread to reach the cond_wait
		pthread_cond_signal (&t_context->sync_cond);
		while (wait_for_thread && t_context->waiting_signal) { //if needed wait for the thread to have received the signal
			usleep(1000);
			pthread_cond_signal(&t_context->sync_cond);
		}
	}else{
		fprintf(stderr, "Invalid thread id %d\n", thread_index);
	}
}

void wait_signal(thread_context * context, char * args){
	if (debug) printf("Waiting for signal to continue in thread %d.\n", context->id);
	if (context->id==0) {
		fprintf(stderr, "wait_signal not possible on main thread %d\n", context->id);
		return; //don't do on main thread
	}
	pthread_mutex_lock (&context->sync_mutex);
	context->waiting_signal=1;
	pthread_cond_wait (&context->sync_cond, &context->sync_mutex);
	context->waiting_signal=0;
	pthread_mutex_unlock (&context->sync_mutex);
	if (debug) printf("Signal received in thread %d.\n", context->id);
}

//prints text on output, for debugging large scripts
void echo(thread_context* context, char* args) {
	write_to_output(args);
	write_to_output("\n");
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
void execute_command(thread_context * context, char * command_line){
    int i;
    if (command_line[0]=='#') return; //=comments
    
    if (context->write_to_thread_buffer > 0){ //inside thread_start -> thread_stop structure, write all commands to a different thread buffer
        if (strncmp(command_line, "thread_stop", 11)==0){
            if (debug) printf("Starting thread.\n");
			start_thread(context->write_to_thread_buffer);
			context->write_to_thread_buffer=0;
        }else{
            if (debug) printf("Write to thread buffer: %s\n", command_line);
            while (*command_line!=0){
                write_thread_buffer(& threads[context->write_to_thread_buffer], *command_line); //for TCP/IP we write to the thread buffer
                command_line++;
            }
            write_thread_buffer(& threads[context->write_to_thread_buffer], ';');
        }
	}else if (context->write_to_own_buffer){ //inside do ... loop in main thread, write all commands to temporary buffer, when reaching the last loop exit from this mode and execute the captured commands
		for (i=0;i<strlen(command_line);i++) write_thread_buffer(context, command_line[i]);
		write_thread_buffer(context, ';');
		if (strncmp(command_line, "loop", 4)==0){
			context->loop_count++;
		}else if (strncmp(command_line, "do", 2)==0){
			context->do_count++;
		}
		if (context->loop_count==context->do_count){
			context->write_to_own_buffer=0;
			context->thread_running=1;
			context->execute_main_do_loop=1; //needed for the start_loop function to know if it must execute the command
			thread_func(context); //now execute captured commands
			context->execute_main_do_loop=0;
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
				for (i=0;i<context->loop_index;i++){
					sprintf(find_loop_nr, "{%d}", i);
					sprintf(replace_loop_index, "%d", context->loops[i].n_loops + context->loops[i].add_to_index);
					str_replace(tmp_arg, arg, find_loop_nr, replace_loop_index); //cannot put result in same string we are replacing, store in temp buffer
					strcpy(arg, tmp_arg);
				}
				free(tmp_arg);
			}
		}
        
        if (strcmp(command, "render")==0){
            render(context, arg);
        }else if (strcmp(command, "rotate")==0){
            rotate(context, arg);
        }else if (strcmp(command, "delay")==0){
            if (arg!=NULL)	usleep((atoi(arg)+1)*1000);
        }else if (strcmp(command, "brightness")==0){
            brightness(context, arg);
        }else if (strcmp(command, "rainbow")==0){
            rainbow(context, arg);
        }else if (strcmp(command, "fill")==0){	
            fill(context, arg);
        }else if (strcmp(command, "fade")==0){
            fade(context, arg);
        }else if (strcmp(command, "gradient")==0){
            gradient(context, arg);
        }else if (strcmp(command, "random")==0){
            add_random(context, arg);
        }else if (strcmp(command, "do")==0){
            start_loop(context, arg);
        }else if (strcmp(command, "loop")==0){
            end_loop(context, arg);
        }else if (strcmp(command, "thread_start")==0){ //start a new thread that processes code, in case thread is already running it will abort or wait depending on the join thread type
            init_thread(context, arg);
        }else if (strcmp(command, "init")==0){ //first init ammount of channels wanted
            init_channels(context, arg);
        }else if (strcmp(command, "setup")==0){ //setup the channels
            setup_ledstring(context, arg);
        }else if (strcmp(command, "settings")==0){
            print_settings();
        }else if (strcmp(command, "global_brightness")==0){
            global_brightness(context, arg);
		}else if (strcmp(command, "blink")==0){
			blink(context, arg);
		}else if (strcmp(command, "random_fade_in_out")==0){
			random_fade_in_out(context, arg);
		}else if (strcmp(command, "chaser")==0){
			chaser(context, arg);
		}else if (strcmp(command, "color_change")==0){
			color_change(context, arg);
		}else if (strcmp(command, "fly_in")==0){
			fly_in(context, arg);
		}else if (strcmp(command, "fly_out")==0){
			fly_out(context, arg);
		}
		else if (strcmp(command, "progress") == 0) {
			progress(context, arg);
#ifdef USE_2D
		}
		else if (strcmp(command, "set_pixel_color") == 0) {
			set_pixel_color(context, arg);
		}
		else if (strcmp(command, "config_2D") == 0) {
			config_2D(context, arg);
		}
		else if (strcmp(command, "take_screenshot") == 0) {
			take_screenshot(context, arg);
		}
		else if (strcmp(command, "draw_image") == 0) {
			draw_image(context, arg);
		}
		else if (strcmp(command, "print_text") == 0) {
			print_text(context, arg);
		}
		else if (strcmp(command, "cls") == 0) {
			cls(context, arg);
		}
		else if (strcmp(command, "change_layer") == 0) {
			change_layer(context, arg);
		}
		else if (strcmp(command, "message_board") == 0) {
			message_board(context, arg);
		}
		else if (strcmp(command, "draw_rectangle") == 0) {
			draw_rectangle(context, arg);
		}
		else if (strcmp(command, "draw_sharp_line") == 0) {
			draw_sharp_line(context, arg);
		}
		else if (strcmp(command, "draw_line") == 0) {
			draw_line(context, arg);
		}
		else if (strcmp(command, "draw_circle") == 0) {
			draw_circle(context, arg);
		}
		else if (strcmp(command, "init_layer") == 0) {
			init_layer(context, arg);
		}
		else if (strcmp(command, "text_input") == 0) {
			text_input(context, arg);
		#endif //USE_2D
		#ifdef USE_AUDIO
		} else if (strcmp(command, "record_audio") == 0) {
			record_audio(context, arg);
		}
		else if (strcmp(command, "pulses") == 0) {
			audio_pulses(context, arg);
		}
		else if (strcmp(command, "light_organ") == 0) {
			light_organ(context, arg);
		}else if (strcmp(command, "vu_meter")==0){
			vu_meter(context, arg);
		#endif
		#ifdef USE_JPEG
		}else if (strcmp(command, "readjpg")==0){
			readjpg(context, arg);
		#endif
		#ifdef USE_PNG
		}else if (strcmp(command, "readpng")==0){
			readpng(context, arg);
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
			printf("random_fade_in_out <channel>,<duration Sec>,<count>,<delay>,<step>,<sync_delay>,<inc_dec>,<brightness>,<start>,<len>,<color>");
			printf("chaser <channel>,<duration>,<color>,<count>,<direction>,<delay>,<start>,<len>,<brightness>,<loops>\n");
			printf("color_change <channel>,<startcolor>,<stopcolor>,<duration>,<start>,<len>\n");
			printf("fly_in <channel>,<direction>,<delay>,<brightness>,<start>,<len>,<start_brightness>,<color>\n");
			printf("fly_out <channel>,<direction>,<delay>,<brightness>,<start>,<len>,<end_brightness>,<color>\n");
			printf("progress <channel>,<direction>,<delay>,<start>,<len>,<brightness_on>,<brightness_off>,<value>\n");
			printf("save_state <channel>,<file_name>,<start>,<len>\n");
			printf("load_state <channel>,<file_name>,<start>,<len>\n");
			#ifdef USE_JPEG
			printf("readjpg <channel>,<file>,<LED start>,<len>,<JPEG Pixel offset>,<OR,AND,XOR,NOT,=>,<flip even rows 1/0>\n");
			#endif
			#ifdef USE_PNG
			printf("readpng <channel>,<file>,<BACKCOLOR>,<LED start>,<len>,<PNG Pixel offset>,<OR,AND,XOR,NOT,=>,<flip even rows 1/0>\n     BACKCOLOR=XXXXXX for color, PNG=USE PNG Back color (default), W=Use alpha for white leds in RGBW strips.\n");
			#endif
            printf("settings\n");
            printf("do <start_index>,<add_to_index> ... loop <end_index>,<increment_index>\n");
			printf("thread_start <id>,<join_type> .... thread_stop\n");
			printf("kill_thread <id>,<join_type>\n");
			printf("signal_thread <id>,<1=wait for thread to receive signal>\n");
			printf("wait_signal\n");
			printf("wait_thread\n");
			printf("echo <text>\n");
			printf("Inside a finite loop {x} will be replaced by the current loop index number. x stands for the loop number in case of multiple nested loops (default use 0).\n");
            printf("exit\n");
        }else if (strcmp(command, "save_state")==0){
			save_state(context, arg);
		}else if (strcmp(command, "load_state")==0){
			load_state(context, arg);
		}else if (strcmp(command, "set_thread_exit_type")==0){
			set_thread_exit_type(context, arg);
		}else if (strcmp(command, "wait_thread_exit")==0){
			wait_thread(context, arg);
		}else if (strcmp(command, "kill_thread")==0){
			kill_thread(context, arg);
		}else if (strcmp(command, "wait_signal")==0){
			wait_signal(context, arg);
		}else if (strcmp(command, "signal_thread") == 0) {
			signal_thread(context, arg);
		}else if (strcmp(command, "echo")==0){
			echo(context, arg);
		}else if (strcmp(command, "debug") == 0) {
			if (debug) debug = 0;
			else debug = 1;
		}else if (strcmp(command, "reset") == 0){
			reset_led_strings();
        }else if (strcmp(command, "exit")==0){
            printf("Exiting.\n");
            exit_program=1;
        }else{
            printf("Unknown cmd: %s\n", command_line);
        }
		if (arg!=NULL) free(arg);
    }
}

void process_character(thread_context * context, char c){
    if (c=='\n' || c == '\r' || c == ';'){
        if (context->command_index>0){
            context->command_line[context->command_index]=0; //terminate with 0
            execute_command(context, context->command_line);
            context->command_index=0;
        }
    }else{
        if (!(context->command_index==0 && (c == ' ' || c == '\t'))){
            context->command_line[context->command_index]=(char)c;
            context->command_index++;
            if (context->command_index==context->command_line_size) context->command_index=0;
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
    
	if (active_socket!=-1) close(active_socket);
    
    printf("Waiting for client to connect.\n");
    
    clilen = sizeof(cli_addr);
    active_socket = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
	if (active_socket!=-1){
		if (setsockopt(active_socket, SOL_SOCKET, SO_KEEPALIVE, &sock_opt, optlen)){
			perror("Error set SO_KEEPALIVE\n");
		}

		printf("Client connected.\n");
	}else{
		perror("Socket accept error");
	}		
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
	 
	 printf("Listening on %d.\n", port);
     listen(sockfd,5);
     tcp_wait_connection();
}

void load_config_file(char * filename){
	FILE * file = fopen(filename, "r");
	
	if (debug) printf("Reading config file %s\n", filename);
	
	if (file == NULL) {
		perror("Unable to open config file.");
		return;
	}

	char line[1024];
    while (fgets(line, sizeof(line), file) != NULL) {
		char * val = strchr(line, '=');
		char * cfg =  strtok(line, " =\t\r\n");
		
		if (val!=NULL) val++;
		if (debug) printf("Reading Config line %s, cfg=%s, val=%s\n", line, cfg, val);
		
		if (val!=NULL) val = strtok(val, "\r\n");
		while (val!=NULL && val[0]!=0 && (val[0]==' ' || val[0]=='\t')) val++;
		
		
		if (strcmp(cfg, "mode")==0 && val!=NULL){
			if (debug) printf("Setting mode %s\n", val);
			if (strcmp(val, "tcp")==0){
				mode = MODE_TCP;
			}else if (strcmp(val, "file")==0){
				mode = MODE_FILE;
			}else if (strcmp(val, "pipe")==0){
				mode = MODE_NAMED_PIPE;
			}else{
				fprintf(stderr, "Unknown mode %s\n", val);
			}
		}else if (strcmp(cfg, "file")==0 && val!=NULL){ 
			if (mode==MODE_FILE){
				if (debug) printf("Setting input file %s\n", val);
				input_file = fopen(val, "r");
			}
		}else if (strcmp(cfg, "port")==0 && val!=NULL){
			if (mode==MODE_TCP){
				port = atoi(val);
				if (port==0) port=9999;
				if (debug) printf("Using TCP port %d\n", port);
			}
		}else if (strcmp(cfg, "pipe")==0 && val!=NULL){
			if (mode==MODE_NAMED_PIPE){
				if (debug) printf("Opening named pipe %s\n", val);
				named_pipe_file = (char*)malloc(strlen(val)+1);
				strcpy(named_pipe_file, val);
				remove(named_pipe_file);
				mkfifo(named_pipe_file,0777);
				chmod(named_pipe_file,0777);
				input_file = fopen(named_pipe_file, "r");
			}
		}else if (strcmp(cfg, "init")==0 && val!=NULL){
			if (strlen(val)>0){
				if (debug) printf("Initialize cmd: %s\n", val);
				initialize_cmd = (char*)malloc(strlen(val)+1);			
				strcpy(initialize_cmd, val);
			}
		}
        
    }

    fclose(file);
}

//main routine
int main(int argc, char* argv[]) {
	int ret = 0;
	int i, j;
	int index = 0;

   
    srand (time(NULL));

	memset(led_channels, 0, sizeof(led_channels));
	reset_led_strings();
    
	pthread_mutex_init(&ws2812_render_mutex, NULL);
	for (i=0;i<MAX_THREADS;i++){
		threads[i].thread=0;
		pthread_mutex_init(&threads[i].sync_mutex,NULL);
		pthread_cond_init(&threads[i].sync_cond,NULL);
		threads[i].end_current_command=0;
		threads[i].thread_data=NULL;
		threads[i].command_line=NULL;
		threads[i].command_line_size=0;
		threads[i].write_to_thread_buffer=0;
		threads[i].write_to_own_buffer=0;
		threads[i].thread_read_index=0;
		threads[i].thread_write_index=0;
		threads[i].thread_data_size=0;
		threads[i].thread_running=0;
		threads[i].command_index=0;      //current position
		threads[i].loop_index=0;       //current loop index
		threads[i].thread_join_type=JOIN_THREAD_CANCEL;
		threads[i].id=i;
		threads[i].do_count=0;
		threads[i].loop_count=0;
		threads[i].write_to_own_buffer=0;
		threads[i].execute_main_do_loop=0;
#ifdef USE_2D
		threads[i].ft_init = false;
#endif
		for(j=0;j<MAX_LOOPS;j++){
			threads[i].loops[j].do_pos=0;      //positions of 'do' in file loop, max 32 loops
			threads[i].loops[j].n_loops=0;
		}
	}
	
    named_pipe_file=NULL;
	malloc_command_line(&threads[0],DEFAULT_COMMAND_LINE_SIZE);

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
            printf ("Opening %s as named pipe.\n", named_pipe_file);
            remove(named_pipe_file);
            mkfifo(named_pipe_file,0777);
            chmod(named_pipe_file,0777);
            input_file = fopen(named_pipe_file, "r");
            mode  = MODE_NAMED_PIPE;
        }else if (strcmp(argv[arg_idx], "-f")==0){ //read commands / data from text file
            if (argc>arg_idx+1){
                input_file = fopen(argv[arg_idx+1], "r");
                printf("Opening %s.\n", argv[arg_idx+1]);
				if (input_file == NULL) {
					perror("Error opening command file.");
					return 1;
				}
				arg_idx++;
            }else{
                fprintf(stderr,"Error you must enter a file name after -f option\n");
                exit(1);
            }
            mode = MODE_FILE;
        }else if (strcmp(argv[arg_idx], "-tcp")==0){ //open up tcp ip port and read commands from there
            if (argc>arg_idx+1){
                port = atoi(argv[arg_idx+1]);
                if (port==0) port=9999;
				arg_idx++;
				mode = MODE_TCP;
            }else{
                fprintf(stderr,"You must enter a port after -tcp option\n");
                exit(1);
            }
		}else if (strcmp(argv[arg_idx], "-c")==0){ //load configuration file
			if (argc>arg_idx+1){
				load_config_file(argv[arg_idx+1]);
			}else{
				fprintf(stderr,"No configuration file given!\n");
				exit(1);
			}
        }else if (strcmp(argv[arg_idx], "-d")==0){ //turn debug on
			debug=1;
		}else if (strcmp(argv[arg_idx], "-i")==0){ //initialize command
			if (argc>arg_idx+1){
				arg_idx++;
				initialize_cmd = (char*)malloc(strlen(argv[arg_idx])+1);
				strcpy(initialize_cmd, argv[arg_idx]);
			}
		}else if (strcmp(argv[arg_idx], "-?")==0){
			printf("WS2812 Server program for Raspberry Pi V6.1\n");
			printf("Command line options:\n");
			printf("-p <pipename>       	creates a named pipe at location <pipename> where you can write command to.\n");
			printf("-f <filename>       	read commands from <filename>\n");
			printf("-tcp <port>         	listen for TCP connection to receive commands from.\n");
			printf("-d                  	turn debug output on.\n");
			printf("-i \"<commands>\"       initialize with <commands> (seperate and end with a ;)\n");
			printf("-c <filename>		    initializes using a configuration file (for running as deamon)\n");
			printf("-?                  	show this message.\n");
			return 0;
		}
		arg_idx++;
	}
	
	if ((mode == MODE_FILE || mode == MODE_NAMED_PIPE) && input_file==NULL){
		fprintf(stderr,"Error opening file!\n");
		exit(1);
	}
	
    int c;
	
	if (initialize_cmd!=NULL){
		for(i=0;i<strlen(initialize_cmd);i++){
			process_character(& threads[0], initialize_cmd[i]);
		}
		free(initialize_cmd);
		initialize_cmd=NULL;
	}
	
	if (mode==MODE_TCP) start_tcpip(port);
	
	while (exit_program==0) {
        if (mode==MODE_TCP){
            c = 0;
            if (read(active_socket, (void *) & c, 1)<=0) c = EOF; //returns 0 if connection is closed, -1 if no more data available and >0 if data read
        }else{
            c = fgetc (input_file); //doesn't work with tcp
        }
        
		if (c!=EOF){
			process_character(& threads[0], c);
		}else{
			//end of file or read error
			switch (mode){
				case MODE_TCP:
					if (!exit_program){
						tcp_wait_connection(); //go back to wait for connection
					}
					break;
				case MODE_NAMED_PIPE:
					input_file = fopen(named_pipe_file, "r");
					//remove(named_pipe_file);
					//mkfifo(named_pipe_file, 0777);
					//chmod(named_pipe_file, 0777);
					break;
				case MODE_STDIN:
					usleep(10000);
					break;
				case MODE_FILE:
					process_character(& threads[0],'\n'); //end last line
					exit_program=1; 
					//if (ftell(input_file)==feof(input_file))  exit_program=1; //exit the program if we reached the end
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
	
	for (i=0; i<MAX_THREADS;i++){
		if (i>0 && threads[i].thread_running){
			threads[i].thread_running=0;
			threads[i].end_current_command=1;
			if (threads[i].waiting_signal){
				pthread_cond_signal (&threads[i].sync_cond);
			}
			pthread_join(threads[i].thread,NULL);
		}
		if (threads[i].thread_data!=NULL) free(threads[i].thread_data);
		if (threads[i].command_line!=NULL) free(threads[i].command_line);
		threads[i].thread_data=NULL;
		threads[i].command_line=NULL;
		pthread_mutex_destroy(&threads[i].sync_mutex);
		pthread_cond_destroy(&threads[i].sync_cond);
	}
	

	//if (thread_data!=NULL) free(thread_data);
    if (ws2811_ledstring.device!=NULL) ws2811_fini(&ws2811_ledstring);
	sk9822_fini(&sk9822_ledstring);

    return ret;
}
