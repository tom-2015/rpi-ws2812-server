#ifndef _SIMPLE_SPI_H
    #define _SIMPLE_SPI_H

    #include <linux/spi/spidev.h>	//Needed for SPI port
    typedef int spi_device_handle;
    
    //opens device
    //spi_device: path to the spi device you want to use "/dev/spidev0.1" or "/dev/spidev0.0"
    //spi_bitsPerWord 8 for 8 bit transfers
    //spi_speed: speed you want to run, note that it will be round down to the nearest possible speed of the hardware!
    //MODE:
    //SPI_MODE_0 (0,0) 	CPOL = 0, CPHA = 0, Clock idle low, data is clocked in on rising edge, output data (change) on falling edge
    //SPI_MODE_1 (0,1) 	CPOL = 0, CPHA = 1, Clock idle low, data is clocked in on falling edge, output data (change) on rising edge
    //SPI_MODE_2 (1,0) 	CPOL = 1, CPHA = 0, Clock idle high, data is clocked in on falling edge, output data (change) on rising edge
    //SPI_MODE_3 (1,1) 	CPOL = 1, CPHA = 1, Clock idle high, data is clocked in on rising, edge output data (change) on falling edge
    //returns spi_device_handle you need to pass to SpiWriteAndRead functions Or NULL if it cannot be opened
    spi_device_handle spi_open_device(const char * spi_device, unsigned char spi_bitsPerWord, int spi_speed, unsigned char spi_mode);
    
    //write data on the SPI interface and read data (always happens at the same time)
    //spi_device = file pointer to read/write to (returned by spiopenport)
    //data_out = data to transmit (can be null to transmit 0)
    //data_in  = where to place bytes read, can be the same pointer as data_out
    //length = number of bytes to write/read
    int spi_write_and_read (spi_device_handle spi_device, unsigned char * data_out, unsigned char * data_in, int length);
    
    //same as SpiWriteAndRead but pauses between each byte read, allowing a slave µc to process data
    //spi_device = file pointer to read/write to (returned by spiopenport)
    //data_out = data to transmit (can be null to transmit 0)
    //data_in  = where to place bytes read, can be the same pointer as data_out
    //length = number of bytes to write/read
    //delay  = number of µsec to wait between each byte
    int spi_write_and_read_delayed (spi_device_handle spi_device, unsigned char * data_out, unsigned char * data_in, int length, int delay); 
    
    //closes the device
    void spi_close_device (spi_device_handle spi_device);

#endif