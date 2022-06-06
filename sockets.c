#include "sockets.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

//return true if socket is still connected
bool socket_connected(socket_t socket_fd){
	int error = 0;
	socklen_t len = sizeof (error);
	int retval = getsockopt (socket_fd, SOL_SOCKET, SO_ERROR, &error, &len);
	if (retval != 0) {
		fprintf(stderr, "Error getting socket error code: %s\n", strerror(retval));
	}
	if (error != 0) {
		fprintf(stderr, "Socket error: %s\n", strerror(error));
	}
	return retval == 0 && error == 0;
}

//waits until the buffer is total received wanted_size
//returns > 0: number of bytes received
//returns < 0: connection closed
int receive_complete_buffer(socket_t socket, void * buffer, unsigned int wanted_size){
	int received = 1; //start
	int total_received=0;
	while (wanted_size>total_received && received > 0){
		received = read(socket, buffer, wanted_size);
		if (received > 0){
			total_received+=received;
			buffer +=received;
		} 
	}
	if (received <= 0) return received;
	return total_received;
}

//sends a complete buffer over the socket
//returns total bytes transferred or < 0 on error
int send_complete_buffer(socket_t socket, void * buffer, unsigned int size, int flags){
	int sent = 0;
	int total_sent = 0;

	while (size > 0){
		sent = send(socket, buffer, size >= 2048 ? 2048 : size, flags);
		if (sent < 0) return sent; //error
		buffer +=sent;
		size -= sent;
		total_sent +=sent;
	}

	return total_sent;

}

//converts IP or hostname to binary in_addr structure
int hostname_to_ip(char * hostname , struct in_addr * addr) {
	struct hostent *he;
	struct in_addr **addr_list;
	int i;
		
	if ( (he = gethostbyname( hostname ) ) == NULL){ // get the host info
		return 1;
	}

	addr_list = (struct in_addr **) he->h_addr_list;
	
	for(i = 0; addr_list[i] != NULL; i++) {
		memcpy(addr, addr_list[i], sizeof(struct in_addr));//Return the first one;
		return 0;
	}
	
	return 1;
}

int bytes_available(socket_t socket_fd){
	int count;
	ioctl(socket_fd, FIONREAD, &count);
	return count;	
}

