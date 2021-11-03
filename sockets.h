#ifndef SOCKETS_HELPER_H
#define SOCKETS_HELPER_H

#include <stdbool.h>
#include <netinet/in.h>
typedef int socket_t;

bool socket_connected(socket_t socket_fd);
int send_complete_buffer(socket_t socket, void * buffer, unsigned int size, int flags);
int receive_complete_buffer(socket_t socket, void * buffer, unsigned int wanted_size);
int hostname_to_ip(char * hostname , struct in_addr * addr);

#endif