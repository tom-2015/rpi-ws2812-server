#include "master_slave.h"
#include "sockets.h"

//closes connection with slave channel, clean up stuff
void close_slave_channel(channel_info * channel){
	if (channel->virtual_channel->remote_socket!=-1){
		close(channel->virtual_channel->remote_socket);
		channel->virtual_channel->remote_socket=-1;
	}
	channel->virtual_channel->wait_for_packet=false;
}

//connects a slave channnel with the remote node
bool connect_slave_channel(channel_info * channel){
	struct sockaddr_in serv_addr;
	memset(&serv_addr, '0', sizeof(serv_addr)); 

	channel->virtual_channel->remote_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (channel->virtual_channel->remote_socket<0){
		fprintf(stderr, "Error connecting slave channel to remote server %d.\n", channel->channel_index+1);
		return false;
	}

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(channel->virtual_channel->remote_port);

    if(hostname_to_ip(channel->virtual_channel->remote_address, &serv_addr.sin_addr)!=0) {
		close_slave_channel(channel);
        fprintf(stderr, "Error reading remote address: %s.\n", channel->virtual_channel->remote_address);
        return false;
    } 

    if(connect(channel->virtual_channel->remote_socket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
	   close_slave_channel(channel);
       fprintf(stderr, "Connecting slave socket %s:%d failed.\n", channel->virtual_channel->remote_address, channel->virtual_channel->remote_port);
       return false;
	}
	int sock_opt=1;
	setsockopt(channel->virtual_channel->remote_socket, SOL_SOCKET, SO_KEEPALIVE, &sock_opt, sizeof(int));
	return true;
}

//sends data to remote slave for rendering a virtual slave channel
//this must be called from render_channel only (because there is a mutex which prevents call from multiple threads)
void render_slave_channel(channel_info * channel){
	slave_channel_packet * packet=NULL;
	int i;
	int data_size = channel->led_count * channel->color_size; //size of the color data
	int total_size = data_size + sizeof(slave_channel_packet); //total size of header + payload

	if (channel->virtual_channel->packet_data==NULL){ //if memory not initialized do it now
		channel->virtual_channel->packet_data = (unsigned char *) malloc(total_size);
	}

	packet = (slave_channel_packet * ) channel->virtual_channel->packet_data;

	packet->signature=SLAVE_SIGNATURE;
	packet->reserved = 0;
	packet->version=SLAVE_PROTOCOL_VERSION;
	packet->command=SLAVE_COMMAND_RENDER;
	packet->flags = 0;
	packet->packet_nr=channel->virtual_channel->packet_nr;
	packet->data_size=data_size;
	
	channel->virtual_channel->packet_nr++; //increase packet counter

	const int scale = (channel->virtual_channel->brightness & 0xff) + 1;
	
	unsigned char * current_led= (unsigned char *) packet + sizeof(slave_channel_packet); //start of first led color after packet header

	//render colors
	for (i=0;i<channel->led_count;i++){
		unsigned int brightness = scale * ((channel->ledstring_1D[i].brightness & 0xff) + 1);
		unsigned int color = channel->ledstring_1D[i].color;

		current_led[0] = ((color & 0xff) * brightness) >> 16; // red
		current_led[1] = (((color >> 8) & 0xff) * brightness) >> 16; // green
		current_led[2] = (((color >> 16) & 0xff) * brightness) >> 16; // blue
		if (channel->color_size == 4 ) current_led[3] = (((color >> 24) & 0xff) * brightness) >> 16; // white
		current_led+=channel->color_size;
	}

	//check connection
	if (channel->virtual_channel->remote_socket==-1 || ! socket_connected(channel->virtual_channel->remote_socket)){
		close_slave_channel(channel);
		if (!connect_slave_channel(channel)){
			if (debug) printf("Connection to slave %s:%d failed\n.", channel->virtual_channel->remote_address, channel->virtual_channel->remote_port);
			return; //connection to slave failed
		} 
	}

	//must wait for confirmation from previous render
	while (channel->virtual_channel->wait_for_packet){
		slave_channel_packet wait_for_packet;
		if (receive_complete_buffer(channel->virtual_channel->remote_socket, &wait_for_packet, sizeof(slave_channel_packet))>0){
			if (wait_for_packet.signature == SLAVE_SIGNATURE){ 
				if (wait_for_packet.command == SLAVE_COMMAND_CMD_OK){
					channel->virtual_channel->wait_for_packet=false;
				}
			}
		}else{
			channel->virtual_channel->wait_for_packet=false; //receive error
		}
	}

	//send all data in 1 packet (or multiple if to large) to remote
	if( send_complete_buffer(channel->virtual_channel->remote_socket, packet, total_size , MSG_NOSIGNAL) < 0){
		if(debug) printf("Error sending data to slave channel %d\n", channel->channel_index+1);
		close_slave_channel(channel);
		return;
	}

	channel->virtual_channel->wait_for_packet = true;

}


bool send_cmd_ak_packet(socket_t slave_socket, int packet_nr){
	slave_channel_packet packet;
	packet.command = SLAVE_COMMAND_CMD_OK;
	packet.data_size = 0;
	packet.flags = 0;
	packet.packet_nr = packet_nr;
	packet.signature = SLAVE_SIGNATURE;
	packet.version = SLAVE_PROTOCOL_VERSION;
	return send_complete_buffer(slave_socket, & packet, sizeof(slave_channel_packet), MSG_NOSIGNAL)>0;
}

//slave_listen channel, port
void slave_listen(thread_context * context, char * args){
	unsigned int channel=0;
	int port;
	
	args = read_channel(args, & channel);

	if (is_valid_channel_number(channel)){
		args = read_int(args, & port);

		socklen_t clilen;
		int sock_opt = 1;
		socklen_t optlen = sizeof (sock_opt);
		int slave_server_socket = -1, slave_socket=-1;
		struct sockaddr_in slave_addr, master_addr;

		//if (active_socket!=-1) close(active_socket);
    
		slave_server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (slave_server_socket < 0) {
			fprintf(stderr,"ERROR opening slave socket.\n");
			return;
		}

		bzero((char *) &slave_addr, sizeof(slave_addr));

		slave_addr.sin_family = AF_INET;
		slave_addr.sin_addr.s_addr = INADDR_ANY;
		slave_addr.sin_port = htons(port);
		if (bind(slave_server_socket, (struct sockaddr *) &slave_addr, sizeof(slave_addr)) < 0) {
			fprintf(stderr,"ERROR on binding port %d.\n", port);
			return;
		}
	 
	 	if (debug) printf("Waiting for master to connect on port %d.\n", port);
     	listen(slave_server_socket,5);

    	clilen = sizeof(master_addr);
    	slave_socket = accept(slave_server_socket, (struct sockaddr *) &master_addr, &clilen);
		if (slave_socket!=-1){
			printf("Master connected.\n");
			if (setsockopt(slave_socket, SOL_SOCKET, SO_KEEPALIVE, &sock_opt, optlen)){
				perror("Error set SO_KEEPALIVE\n");
			}

			slave_channel_packet packet;
			unsigned char * payload=NULL;
			unsigned int payload_size=0;
			bool receive_payload_failed=false;
			bool send_response_failed=false;

			while (!receive_payload_failed && !send_response_failed && socket_connected(slave_socket) && 
					receive_complete_buffer(slave_socket, & packet, sizeof(packet))>0 && context->end_current_command==0){
				if (packet.signature==SLAVE_SIGNATURE){
					if (debug) printf("Received valid slave packet nr %d.\n", packet.packet_nr);
					switch (packet.command){
						case SLAVE_COMMAND_RENDER:
							if(debug) printf("Render slave\n");
							if (packet.data_size>payload_size){
								if (payload_size!=0) free(payload);
								payload_size = packet.data_size;
								payload = malloc(payload_size);
							}

							receive_payload_failed = receive_complete_buffer(slave_socket, payload, payload_size)<=0;
							
							if (!receive_payload_failed){
								ws2811_led_t * ledstring = get_led_string(channel);
								int max_leds = payload_size / get_color_size(channel);
								unsigned int payload_index=0;
								if (max_leds > get_led_count(channel)) max_leds = get_led_count(channel);
								if (get_color_size(channel)==3){
									if (debug) printf("Render 3 colors leds: %d, %d, %d\n", max_leds, payload_size, get_led_count(channel));
									for (unsigned int i=0; i<max_leds;i++){
										ledstring[i].color = (unsigned int)payload[payload_index++] + ((unsigned int)payload[payload_index++] << 8) + ((unsigned int)payload[payload_index++] << 16);
										//payload_index+=3;
									}
								}else{
									if (debug) printf("Render 4 colors leds: %d, %d, %d\n", max_leds, payload_size, get_led_count(channel));
									for (unsigned int i=0; i<max_leds;i++){
										ledstring[i].color = ((unsigned int*)payload)[i]; // color_rgbw(payload[payload_index], payload[payload_index+1], payload[payload_index+2], payload[payload_index+3]);
										//payload_index+=4;
									}
								}
								render_channel(channel);
							}

							break;
					}

					if (!send_cmd_ak_packet(slave_socket, packet.packet_nr)){
						if (debug) printf("Sending response failed.\n");
						 send_response_failed = true;
					}else{
						if (debug) printf("Sending response %d OK.\n", packet.packet_nr);
					}
				}else{
					char buff;
					while (socket_connected(slave_socket) && read(slave_socket, &buff, 1) > 0); //invalid signature, clear buffer
				}
			}

			printf("Master connection lost.\n");
			if (payload_size>0) free(payload);
			close(slave_socket);
		}else{
			perror("Socket accept error");
		}				
		close(slave_server_socket);
	}else{
		fprintf(stderr, ERROR_INVALID_CHANNEL);
	}

}