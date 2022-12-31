
#ifndef WS2812_SVR_H
#define WS2812_SVR_H

#define __USE_C99_MATH
#include <stdbool.h>
#include <cairo.h>
#include <cairo-ft.h>
#include <ft2build.h>
#include <alsa/asoundlib.h>
#include "ws2811.h"
#include "sk9822.h"
#include "pipe.h"

#define DEFAULT_DEVICE_FILE "/dev/ws281x"
#define DEFAULT_COMMAND_LINE_SIZE 1024
#define DEFAULT_BUFFER_SIZE 32768
#define IP_BUFFER_SIZE 64
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
#define CHANNEL_TYPE_SLAVE 100

#define DEFAULT_FONT "monospace"

#define COLOR_TRANSPARENT 0xFF000000

#define JOIN_THREAD_CANCEL 0
#define JOIN_THREAD_WAIT 1


#define CAPTURE_MODE_ALSA 0
#define CAPTURE_MODE_UDP 1
#define CAPTURE_MODE_PIPE 2
#define CAPTURE_MODE_OTHER_THREAD 3

#define FILTER_MODE_NONE 0
#define FILTER_MODE_LOW_PASS 1
#define FILTER_MODE_HIGH_PASS 2
#define FILTER_MODE_BAND_PASS 3

extern volatile int	debug; //1 if debugging output is enabled
extern volatile int	exit_program; //set to 1 to exit the program

typedef struct {
    int do_pos;
    int n_loops; //number of loops made (index)
	int add_to_index; //add this value to the current index variable
} do_loop;

typedef struct {
	snd_pcm_t* handle;
	snd_pcm_hw_params_t* hw_params;
	snd_pcm_format_t format;
	
	pthread_t thread; //capture thread
	volatile int capture_mode; //mode for capturing
	char capture_device_name[MAX_VAL_LEN]; //depends on capture mode, holds audio device name or udp port,...
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
	float capture_gain; //multiply all samples with this factor

	char filter_mode; //FILTER_MODE_*
	float low_pass_filter_coef; //
	float high_pass_filter_coef;
	
	volatile unsigned int copy_from_thread; //thread ID to copy samples from
	volatile pipe_t * sample_pipe; //the pipe, used if copy_from_thread is active to write new samples which can be read by another thread
	volatile pipe_producer_t * sample_pipe_writer; //used by the thread in copy_from_thread to write samples
	volatile pipe_consumer_t * sample_pipe_reader; //used by the thread to read samples
} capture_options;


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

	FT_Library ft_lib;			//needed for .ttf font files in cairo, doc says 1 instance per trhead
	bool ft_init;

	capture_options audio_capture;

} thread_context;

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

typedef struct {
	int color_size;//number of color bytes for 1 led, 3 = BGR and 4 = WBGR
	int count;
	int parent_offset;//offset in the parent channel
	int parent_channel_index;//the parent channel (in case of a virtual channel which affects a part of a main channel)
	int remote_socket; //in case of a slave remote channel, the TCP socket
	char remote_address[IP_BUFFER_SIZE]; //in case of a slave remote channel
	int remote_port; //in case of a slave remote channel
	unsigned char brightness; //only for slave channel, global brightness
	unsigned int packet_nr; //for slave channel, holds next packet nr
	
	volatile bool wait_for_packet; //true, tells the render_slave_channel to wait for this response packet before continue send next data
	unsigned char * packet_data; //pointer for packet payload, used in render_slave_channel for storing channel data
} virtual_channel_t;

#define CHANNEL_FLAG_SKIP_RENDER 1

typedef struct {
	int channel_type;  //defines if this channel is WS2811 or SK9822
	int channel_index; //the channel number in the *_ledstring, at initialization the channel objects are set
	int color_size;		
	int led_count;     //led count
	bool initialized;  //true if initialized
	unsigned int  flags; 	  // flags
	ws2811_channel_t* ws2811_channel;
	sk9822_channel_t* sk9822_channel;
	virtual_channel_t* virtual_channel;
	ws2811_led_t* ledstring_1D; //returns 1D led string
	pthread_mutex_t render_mutex; //only 1 thread should do a render at same time

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

} channel_info;

#define OP_EQUAL 0
#define OP_OR 1
#define OP_AND 2
#define OP_XOR 3
#define OP_NOT 4

void set_output_function (void (* output_func)(char *)); //sets a function that handles returned command text output (transmit it over TCP or named pipe...)
bool init_ws2812svr (); //initializes the library
void terminate_ws2812svr();
bool is_channel_free(int type, int channel_index); //checks if a physical channel_index of channel_type is free or used
int get_free_channel_index(int type); //returns next free channel index for a physical channel type
int get_led_count(int channel_nr); //returns the number of leds in channel_nr
int get_color_size(int channel_nr); //returns number of colors for each LED in a channel
ws2811_led_t* get_led_string(int channel_nr); //returns led string as led type array, for direct access to leds
bool init_ft_lib(thread_context* context); //returns true if freetype library is initialized
unsigned int convert_cairo_color(unsigned int color); //updates the cairo buffer after changes have been made with 1D functions
void cairo_paint_layers(channel_info* chan); //paints all the layer surfaces to the main channel surface
void cairo_reset_layers(channel_info* chan); //resets all layers
void cairo_stroke_rounding(double width, double * x1, double * y1); //converts x and y to create sharp lines in strokes
void render_channel(int channel); //renders a channel
bool is_valid_channel_number(unsigned int channel);
bool is_valid_2D_channel_number(unsigned int channel);
ws2811_led_t*** get_2D_led_string(int channel_nr);
void write_to_output(char * text); //writes text to the output depending on the mode (TCP=write to socket)
unsigned char get_red(int color);
unsigned char get_green(int color);
unsigned char get_blue(int color);
unsigned char get_white(int color);
int color (unsigned char r, unsigned char g, unsigned char b);
void set_cairo_color_rgb(cairo_t * cr, unsigned int color); //sets cairo_source_rgb using internal color GBR
void set_cairo_color_rgba(cairo_t* cr, unsigned int color);
unsigned char alpha_component(unsigned int component, unsigned int bgcomponent, unsigned int alpha);
int color_rgbw (unsigned char r, unsigned char g, unsigned char b, unsigned char w);
int deg2color(unsigned char WheelPos);
char * read_key(char * args, char * key, size_t size);
char * read_val(char * args, char * value, size_t size);
char * read_int(char * args, int * value);
char * read_float(char * args, float * value);
char * read_double(char* args, double * value);
char * read_str(char * args, char * dst, size_t size);
char * read_uint(char * args, unsigned int * value);
char * read_channel(char * args, int * value);
char * read_color(char * args, unsigned int * out_color, unsigned int color_size);
char * read_color_arg(char * args, unsigned int * out_color, unsigned int color_size);
char * read_color_arg_empty(char * args, unsigned int * out_color, unsigned int color_size, bool * arg_empty);
char * read_brightness(char * args, unsigned int * brightness);
char * read_operation(char * args, char * op);
unsigned long long time_ms(); //returns time stamp in ms
void expand_thread_data_buffer(thread_context * context);
void write_thread_buffer (thread_context * context, char c);
void init_channels(thread_context* context, char* args); //initializes channels (init command)
void config_2D(thread_context* context, char* args); //initialize 2D channel (config_2D command)
void reset_led_strings(); //resets all initialized channels and sets memory
void setup_ledstring(thread_context * context, char * args);
void global_brightness(thread_context* context, char* args);
void print_settings();
void render(thread_context * context, char * args);
void save_state(thread_context * context, char * args);
void load_state(thread_context * context, char * args);
void start_loop (thread_context * context, char * args);
void end_loop(thread_context * context, char * args);
void set_thread_exit_type(thread_context * context,char * args);
void init_thread(thread_context * context, char * args);
void thread_func (thread_context * context);
void start_thread(int thread_context_index);
void wait_thread(thread_context * context, char * args);
void kill_thread(thread_context * context, char * args);
bool thread_running(int thread_num); //returns true if thread is running
void stop_command(int thread_index); //stops current command in running thread
void signal_thread(thread_context * context, char * args);
void wait_signal(thread_context * context, char * args);
void echo(thread_context* context, char* args);
void execute_command(thread_context * context, char * command_line);
void process_character(thread_context * context, char c);
void str_replace(char * dst, char * src, char * find, char * replace);
bool file_exists(const char* fname);
thread_context * get_thread(int thread_index);
channel_info * get_channel(int channel_index);
#endif