#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <pthread.h>

#include "clk.h"
#include "gpio.h"
#include "dma.h"
#include "pwm.h"

//ws2811 driver used from:
//https://github.com/jgarff/rpi_ws281x
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

//for easy and fast converting asci hex to integer
static const char hextable[] = {
   [0 ... 255] = 0, // bit aligned access into this table is considerably
   ['0'] = 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, // faster for most modern processors,
   ['A'] = 10, 11, 12, 13, 14, 15,       // for the space conscious, reduce to
   ['a'] = 10, 11, 12, 13, 14, 15        // signed char.
};

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


ws2811_t ledstring; //ws2811 'object'

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

//reads key from argument buffer
//example: channel_1_count=10,
//returns channel_1_count in key buffer, then use read_val to read the 10
char * read_key(char * args, char * key, size_t size){
	size--;
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

//returns a color from RGB value
//note that the ws281x stores colors as GRB
int color (unsigned char r, unsigned char g, unsigned char b){
	return (g << 16) + (r << 8) + b;
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

//initializes the ws2811 driver code
//setup freq=400,dma=5,channels=1,channel_1_gpio=18,channel_1_invert=0,channel_1_count=250,channel_1_brightness=255,channel_2_...
void setup_ledstring(char * args){
	char key[MAX_KEY_LEN], value[MAX_VAL_LEN];
	int key_index, value_index;

	if (ledstring.device!=NULL)	ws2811_fini(&ledstring);
	
	ledstring.device = NULL;
	ledstring.freq = WS2811_TARGET_FREQ;
	ledstring.dmanum = 5;
	ledstring.channel[0].gpionum = 18;
	ledstring.channel[0].invert = 0;
	ledstring.channel[0].count = 1;
	ledstring.channel[0].brightness = 255;
	
	ledstring.channel[1].gpionum = 0;
	ledstring.channel[1].invert = 0;
	ledstring.channel[1].count = 0;
	ledstring.channel[1].brightness = 0;
	
	//char * key = strtok(args, " =");
	//char c = args;
	if (debug) printf("Setup\n");
	
	if (args!=NULL){
		while (*args){
			//first we get the key
			args = read_key(args, key,MAX_KEY_LEN);
			if (*args!=0) *args++;
			//now read the value part
			args = read_val(args, value,MAX_VAL_LEN);
			if (*args!=0) *args++;
			
			if (debug) printf("Setting %s=%s\n", key, value);
			
			if (strlen(key)>0 && strlen(value) > 0){
				if (strcmp(key,"freq")==0){
					ledstring.freq = atoi(value);
				}else if (strcmp(key, "dma")==0){
					ledstring.dmanum = atoi(value);
				}else if (strcmp(key, "channels")==0){
					if (value[0]=='1'){
						ledstring.channel[1].gpionum=0;
						ledstring.channel[1].invert = 0;
						ledstring.channel[1].count = 0;
						ledstring.channel[1].brightness = 0;
					}else{
						ledstring.channel[1].gpionum=0;
						ledstring.channel[1].invert = 0;
						ledstring.channel[1].count = 1;
						ledstring.channel[1].brightness = 255;				
					}
				}else if (strcmp(key, "channel_1_gpio")==0){
					ledstring.channel[0].gpionum=atoi(value);
				}else if (strcmp(key, "channel_1_invert")==0){
					ledstring.channel[0].invert=atoi(value);
				}else if (strcmp(key, "channel_1_count")==0){
					ledstring.channel[0].count=atoi(value);
				}else if (strcmp(key, "channel_1_brightness")==0){
					ledstring.channel[0].brightness=atoi(value);
				}else if (strcmp(key, "channel_2_gpio")==0){
					ledstring.channel[1].gpionum=atoi(value);
				}else if (strcmp(key, "channel_2_invert")==0){
					ledstring.channel[1].invert=atoi(value);
				}else if (strcmp(key, "channel_2_count")==0){
					ledstring.channel[1].count=atoi(value);
				}else if (strcmp(key, "channel_2_brightness")==0){
					ledstring.channel[1].brightness=atoi(value);
				}
			}
			//args++;
		}
	}
	
	int size = ledstring.channel[0].count;
	if (ledstring.channel[1].count> size) size= ledstring.channel[1].count;
	malloc_command_line(DEFAULT_COMMAND_LINE_SIZE + size * 6); //allocate memory for full render data

	if (ws2811_init(&ledstring)){
		perror("Initialization failed.\n");
        return;
    }
	
}

//prints channel settings
void print_settings(){
    printf("Frequency: %d.\n",ledstring.freq);
    printf("DMA num: %d.\n",ledstring.dmanum);
	printf("Channel 1:\n");
    printf("    GPIO: %d\n", ledstring.channel[0].gpionum);
	printf("    Invert: %d\n",ledstring.channel[0].invert);
	printf("    Count: %d\n",ledstring.channel[0].count);
	printf("    Brightness: %d\n",ledstring.channel[0].brightness);
	printf("Channel 2:\n");
    printf("    GPIO: %d\n", ledstring.channel[1].gpionum);
	printf("    Invert: %d\n",ledstring.channel[1].invert);
	printf("    Count: %d\n",ledstring.channel[1].count);
	printf("    Brightness: %d\n",ledstring.channel[1].brightness);
}

//sends the buffer to the leds
//render <channel>,0,AABBCCDDEEFF...
//optional the colors for leds:
//AABBCC are RGB colors for first led
//DDEEFF is RGB for second led,...
void render(char * args){
	int channel=0;
	int r,g,b;
	int led_index;
	int size;
    int start;
    char value[MAX_VAL_LEN];
    char color_string[6];
    
	if (debug) printf("Render %s\n", args);
	
    if (ledstring.device==NULL){
        printf("Error you must call setup first!\n");
        return;
    }
    
    if (args!=NULL){
		args = read_val(args, value, MAX_VAL_LEN);
		channel = atoi(value)-1;
        if (channel<0 || channel > 1) channel=0;
		if (*args!=0){
			args++;
			args = read_val(args, value, MAX_VAL_LEN); //read start position
			start = atoi(value);
			while (*args!=0 && (*args==' ' || *args==',')) args++; //skip white space
            
			if (debug) printf("Render channel %d selected start at %d leds %d\n", channel, start, ledstring.channel[channel].count);
			
            size = strlen(args);
            led_index = start % ledstring.channel[channel].count;

            while (*args!=0){
                //if (debug) printf("r %d %d\n", hextable[args[0]], hextable[args[1]]);
                int i=0;
                while (*args!=0 && i<6){
                    if (*args!=' ' && *args!='\t'){
                        color_string[i]=*args;
                        i++;
                    }
                    args++;
                }
                if (i==6){
                    r = (hextable[color_string[0]]<<4) + hextable[color_string[1]];
                    g = (hextable[color_string[2]]<<4) + hextable[color_string[3]];
                    b = (hextable[color_string[4]]<<4) + hextable[color_string[5]];
                    if (debug) printf(" set pixel %d to %d,%d,%d\n", led_index, r,g,b);
                    ledstring.channel[channel].leds[led_index] = color(r,g,b);
                    led_index += (led_index %ledstring.channel[channel].count);
                }
            }
		}
	}
	if (ledstring.device!=NULL) ws2811_render(&ledstring);
}

//shifts all colors 1 position
//rotate <channel>,<places>,<direction>,<new_color>
//if new color is set then the last led will have this color instead of the color of the first led
void rotate(char * args){
	char value[MAX_VAL_LEN];
	int channel=0, nplaces=1, direction=1, new_color=-1;
	
	if (args!=NULL){
		args = read_val(args, value, MAX_VAL_LEN);
		channel = atoi(value)-1;
		if (*args!=0){
			args++;
			args = read_val(args, value, MAX_VAL_LEN);
			nplaces = atoi(value);
			if (*args!=0){
				args++;
				args = read_val(args, value, MAX_VAL_LEN);
				direction = atoi(value);
				if (*args!=0){
					args++;
					args = read_val(args, value, MAX_VAL_LEN);
					if (strlen(value)==6){
						new_color = color((hextable[value[0]]<<4) + hextable[value[1]],(hextable[value[2]]<<4) + hextable[value[3]], (hextable[value[4]]<<4) + hextable[value[5]]);
					}else{
						printf("Invalid color\n");
					}
				}
			}
		}
	}
	
	if (debug) printf("Rotate %d %d %d %d\n", channel, nplaces, direction, new_color);
	
	int tmp,i,n;
	for(n=0;n<nplaces;n++){
		if (direction==1){
			tmp = ledstring.channel[channel].leds[0];
			for(i=1;i<ledstring.channel[channel].count;i++){
				ledstring.channel[channel].leds[i-1] = ledstring.channel[channel].leds[i]; 
			}
			if (new_color!=-1)
				ledstring.channel[channel].leds[ledstring.channel[channel].count-1]=new_color;
			else
				ledstring.channel[channel].leds[ledstring.channel[channel].count-1]=tmp;
		}else{
			tmp = ledstring.channel[channel].leds[ledstring.channel[channel].count-1];
			for(i=ledstring.channel[channel].count-1;i>0;i--){
				ledstring.channel[channel].leds[i] = ledstring.channel[channel].leds[i-1]; 
			}
			if (new_color!=-1)
				ledstring.channel[channel].leds[0]=new_color;		
			else
				ledstring.channel[channel].leds[0]=tmp;		
		}
	}
}

//fills pixels with rainbow effect
//count tells how many rainbows you want
//rainbow <channel>,<count>,<start>,<stop>
//start and stop = color values on color wheel (0-255)
void rainbow(char * args) {
	char value[MAX_VAL_LEN];
	int channel=0, count=1,start=0,stop=255;
	
	if (args!=NULL){
		args = read_val(args, value, MAX_VAL_LEN);
		channel = atoi(value)-1;
		if (*args!=0){
			args++;
			args = read_val(args, value, MAX_VAL_LEN);
			count = atoi(value);
			if (*args!=0){
				args++;
				args = read_val(args, value, MAX_VAL_LEN);
				start = atoi(value);
				if (*args!=0){
					args++;
					args = read_val(args, value, MAX_VAL_LEN);
					stop = atoi(value);
				}
			}
		}
	}	
	
	if (channel!=0 && channel!=1) channel=0;
	if (start<0 || start > 255) start=0;
	if (stop<0 || stop > 255) stop = 255;
	
	if (debug) printf("Rainbow %d,%d\n", channel, count);
	
	int numPixels = ledstring.channel[channel].count;
	int i, j;
	for(i=0; i<numPixels; i++) {
		ledstring.channel[channel].leds[i] = deg2color(abs(stop-start) * i * count / numPixels + start);
	}
}

//fills leds with certain color
//fill <channel>,<color>,<start>,<len>
void fill(char * args){
	char value[MAX_VAL_LEN];
	int channel=0, fill_color=0,start=0,len=-1;
	
	if (args!=NULL){
		args = read_val(args, value, MAX_VAL_LEN);
		channel = atoi(value)-1;
		if (*args!=0){
			args++;
			args = read_val(args, value, MAX_VAL_LEN);
			if (strlen(value)==6){
				fill_color = color((hextable[value[0]]<<4) + hextable[value[1]],(hextable[value[2]]<<4) + hextable[value[3]], (hextable[value[4]]<<4) + hextable[value[5]]);
			}else{
				printf("Invalid color\n");
			}
			if (debug) printf(args);
			if (*args!=0){
				args++;
				args = read_val(args, value, MAX_VAL_LEN);
				start = atoi(value);
				if (*args!=0){
					args++;
					args = read_val(args, value, MAX_VAL_LEN);
					len = atoi(value);
				}
			}
		}
	}
	
	if (channel!=0 && channel!=1) channel=0;
	if (len<=0 || len>ledstring.channel[channel].count) len=ledstring.channel[channel].count;
	if (start<0 || start>=ledstring.channel[channel].count) start=0;
	
	if (debug) printf("fill %d,%d,%d,%d\n", channel, fill_color, start, len);
	
	int i;
	for (i=start;i<start+len;i++){
		ledstring.channel[channel].leds[i]=fill_color;
	}
}

//dims leds (adjust brightness of all on channel)
//brightness <channel>,<brightness> (brightness: 0-255)
void brightness(char * args){
	char value[MAX_VAL_LEN];
	int channel=0, brightness=255;
	
	if (args!=NULL){
		args = read_val(args, value, MAX_VAL_LEN);
		channel = atoi(value)-1;
		if (*args!=0){
			args++;
			args = read_val(args, value, MAX_VAL_LEN);
			brightness = atoi(value);
		}
	}
	
	if (channel!=0 && channel!=1) channel=0;
	if (brightness<0 || brightness>0xFF) brightness=255;
	
	if (debug) printf("Changing brightness %d, %d\n", channel, brightness);
	
	ledstring.channel[channel].brightness=brightness;

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
 
        char * arg = strchr(command_line, ' ');
        char * command =  strtok(command_line, " \r\n");    
        
        if (arg!=NULL) arg++;
        
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
        }else if (strcmp(command, "do")==0){
            start_loop(arg);
        }else if (strcmp(command, "loop")==0){
            end_loop(arg);
        }else if (strcmp(command, "thread_start")==0){ //start a new thread that processes code
            if (thread_running==0 && mode==MODE_TCP) init_thread(arg);
        }else if (strcmp(command, "setup")==0){
            setup_ledstring(arg);
        }else if (strcmp(command, "settings")==0){
            print_settings();
        }else if (strcmp(command, "debug")==0){
            if (debug) debug=0;
            else debug=1;
        }else if (strcmp(command, "exit")==0){
            printf("Exiting.\n");
            exit_program=1;
        }else{
            printf("Unknown cmd: %s\n", command_line);
        }
    }
}

void process_character(char c){
    if (c=='\n' || c == '\r' || c == ';' || c=='\n'){
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
        perror("ERROR opening socket\n");
        exit(1);
     }

     bzero((char *) &serv_addr, sizeof(serv_addr));

     serv_addr.sin_family = AF_INET;
     serv_addr.sin_addr.s_addr = INADDR_ANY;
     serv_addr.sin_port = htons(port);
     if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR on binding.\n");
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

	ledstring.device=NULL;
	command_line = NULL;
    named_pipe_file=NULL;
	malloc_command_line(DEFAULT_COMMAND_LINE_SIZE);

    setup_handlers();

    input_file = stdin; //by default we read from console, stdin
    mode = MODE_STDIN;
    
	if (argc>1){
        if (strcmp(argv[1], "-p")==0){ //use a named pipe, creates a file (by default in /dev/ws281x) which you can write commands to: echo "command..." > /dev/ws281x
            if (argc>2){
                named_pipe_file = malloc(strlen(argv[2]+1));
                strcpy(named_pipe_file,argv[2]);
            }else{
                named_pipe_file = malloc(strlen(DEFAULT_DEVICE_FILE)+1);
                strcpy(named_pipe_file, DEFAULT_DEVICE_FILE);
            }
            printf ("Opening %s as named pipe.", named_pipe_file);
            remove(named_pipe_file);
            mkfifo(named_pipe_file,0777);
            chmod(named_pipe_file,0777);
            input_file = fopen(named_pipe_file, "r");
            mode  = MODE_NAMED_PIPE;
        }else if (strcmp(argv[1], "-f")==0){ //read commands / data from text file
            if (argc>2){
                input_file = fopen(argv[2], "r");
                printf("Opening %s.", argv[2]);
            }else{
                printf("Error you must enter a file name after -f option\n");
                exit(1);
            }
            mode = MODE_FILE;
        }else if (strcmp(argv[1], "-tcp")==0){ //open up tcp ip port and read commands from there
            if (argc>2){
                int port = atoi(argv[2]);
                if (port==0) port=9999;
                printf("Listening on %d.\n", port);
                start_tcpip(port);
            }else{
                printf("You must enter a port after -tcp option\n");
                exit(1);
            }
            mode = MODE_TCP;
        }
	}
	
	if ((mode == MODE_FILE || mode == MODE_NAMED_PIPE) && input_file==NULL){
		perror("Error opening file!");
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
                exit_program=1;
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

