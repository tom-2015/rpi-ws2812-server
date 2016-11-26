#include "spi.h"
#include <unistd.h>
#include <fcntl.h>				//Needed for SPI port
#include <sys/ioctl.h>			//Needed for SPI port
#include <memory.h>
//http://elinux.org/RPi_SPI
//----- SET SPI MODE -----
//SPI_MODE_0 (0,0) 	CPOL = 0, CPHA = 0, Clock idle low, data is clocked in on rising edge, output data (change) on falling edge
//SPI_MODE_1 (0,1) 	CPOL = 0, CPHA = 1, Clock idle low, data is clocked in on falling edge, output data (change) on rising edge
//SPI_MODE_2 (1,0) 	CPOL = 1, CPHA = 0, Clock idle high, data is clocked in on falling edge, output data (change) on rising edge
//SPI_MODE_3 (1,1) 	CPOL = 1, CPHA = 1, Clock idle high, data is clocked in on rising, edge output data (change) on falling edge

spi_device_handle spi_open_device (const char * spi_device, unsigned char spi_bitsPerWord, int spi_speed, unsigned char spi_mode){
	spi_device_handle dev;
    int status_value;
    
    dev = open(spi_device, O_RDWR);
    if (dev < 0) return 0;

    status_value = ioctl((int)dev, SPI_IOC_WR_MODE, &spi_mode);
    if(status_value < 0) return 0;

    status_value = ioctl((int)dev, SPI_IOC_RD_MODE, &spi_mode);
    if(status_value < 0) return 0;

    status_value = ioctl((int)dev, SPI_IOC_WR_BITS_PER_WORD, &spi_bitsPerWord);
    if(status_value < 0) return 0;

    status_value = ioctl((int)dev, SPI_IOC_RD_BITS_PER_WORD, &spi_bitsPerWord);
    if(status_value < 0)return 0;

    status_value = ioctl((int)dev, SPI_IOC_WR_MAX_SPEED_HZ, &spi_speed);
    if(status_value < 0) return 0;

    status_value = ioctl((int)dev, SPI_IOC_RD_MAX_SPEED_HZ, &spi_speed);
    if(status_value < 0) return 0;
    
    return dev;
}


int spi_write_and_read (spi_device_handle spi_device, unsigned char * data_out, unsigned char * data_in, int length){
	struct spi_ioc_transfer spi;

	memset(&spi, 0, sizeof(spi));
    
	spi.tx_buf        = (unsigned long)(data_out); // transmit from "data"
	spi.rx_buf        = (unsigned long)(data_in) ; // receive into "data"
	spi.len           = length;

	return ioctl((int) spi_device, SPI_IOC_MESSAGE(1), &spi) ; //1 SPI operation
}


int spi_write_and_read_delayed (spi_device_handle spi_device, unsigned char * data_out, unsigned char * data_in, int length, int delay){
	struct spi_ioc_transfer spi[length];

	memset(&spi, 0, sizeof(spi));
    
	//one spi transfer for each byte
    for (int i=0;i<length;i++){
        spi[i].tx_buf        = (unsigned long)(data_out+i); // transmit from "data"
        spi[i].rx_buf        = (unsigned long)(data_in+i); // receive into "data"
        spi[i].len           = 1;
        spi[i].delay_usecs   = delay;
	}

	return ioctl((int) spi_device, SPI_IOC_MESSAGE(length), &spi) ; //1 SPI operation
}

void spi_close_device (spi_device_handle spi_device){
    close ((int) spi_device);
}