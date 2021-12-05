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
#include <netdb.h>
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
#include "sockets.h"
#include "ws2812svr.h"
#include "master_slave.h"

FILE *			input_file;         		//the named pipe handle
char *			named_pipe_file;    		//holds named pipe file name 
char *			initialize_cmd=NULL; 		//initialze command
int				mode=MODE_STDIN;            //mode we operate in (TCP, named pipe, file, stdin)



//for TCP mode
socket_t sockfd;        	  //socket that listens
socket_t active_socket=-1; //current active connection socket
socklen_t clilen;
struct sockaddr_in serv_addr, cli_addr;
int port=0;


//handles exit of program with CTRL+C
static void ctrl_c_handler(int signum){
	int i;

	signal(signum, SIG_IGN);

	thread_context * thread;
	if (thread_running(0)){ //interrupt loop in main thread
		thread = get_thread(0);
		thread->end_current_command=1;
		thread->thread_running=0;
		signal(SIGINT, ctrl_c_handler);
	}else{ //exit the program
		for (i=0; i<MAX_THREADS+1;i++){
			thread = get_thread(i);
			thread->end_current_command=1;
			thread->thread_running=0;
		}
		exit_program=1;
		exit(0);
	}
}

//handles broken pipes (send on disconnected socket)
static void sig_pipe_handler(int signum){
	fprintf(stderr, "SIG_PIPE error\n");
	signal(signum, SIG_IGN);
}

static void setup_handlers(void){
	signal(SIGINT, ctrl_c_handler);
	signal(SIGPIPE, sig_pipe_handler);
    //struct sigaction sa;
    //sa.sa_handler = ctrl_c_handler,
    //sigaction(SIGTERM, &sa, NULL); //SIGKILL
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

void write_to_output_fun(char * text){
	switch(mode){
		case MODE_TCP:
			if (active_socket!=-1) {
				if (debug) printf("Sending to client: %s", text);
				send(active_socket,  text, strlen(text), MSG_NOSIGNAL);
			}
			break;
		case MODE_FILE:
		case MODE_STDIN:
			printf(text);
	}
}

//main routine
int main(int argc, char* argv[]) {
	int ret = 0;
	int i, j;
	int index = 0;

    setup_handlers();
	set_output_function(&write_to_output_fun);
	init_ws2812svr(); //setup library
	
    named_pipe_file=NULL;

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
			printf("WS2812 Server program for Raspberry Pi V6.7\n");
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

	thread_context * main_thread = get_thread(0);
	
	if (initialize_cmd!=NULL){	
		for(i=0;i<strlen(initialize_cmd);i++){
			process_character(main_thread, initialize_cmd[i]);
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
			process_character(main_thread, c);
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
					process_character(main_thread,'\n'); //end last line
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
	


    return ret;
}
