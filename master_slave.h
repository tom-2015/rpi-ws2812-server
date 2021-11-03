#ifndef MASTER_SLAVE_H
#define MASTER_SLAVE_H

#include "ws2812svr.h"

#define SLAVE_SIGNATURE 0xFB
#define SLAVE_PROTOCOL_VERSION 0x1
#define SLAVE_COMMAND_RENDER 0x1
#define SLAVE_COMMAND_CMD_OK 0x2

typedef struct {
	unsigned char signature;
	unsigned char version;
	unsigned short reserved;
	unsigned int command;
	unsigned int flags;
	unsigned int packet_nr;
	unsigned int data_size;
} slave_channel_packet;

void close_slave_channel(channel_info * channel);
bool connect_slave_channel(channel_info * channel);
void render_slave_channel(channel_info * channel);
void slave_listen(thread_context * context, char * args);


#endif