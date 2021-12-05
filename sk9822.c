#include "sk9822.h"
#include "spi.h"
#include "mailbox.h"
#include "gpio.h"
#include "rpihw.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

void create_sk9822(sk9822_t* sk9822)
{
    int i;
    for (i = 0;i < RPI_MAX_SPI_DEVICE;i++) {
        sk9822_channel_t* channel = &sk9822->channels[i];
        channel->color_size = 0;
        channel->gpionum = 0;
        channel->invert = 0;
        channel->count = 0;
        channel->strip_type = SK9822_STRIP;
        channel->leds = NULL;
        channel->brightness = 0;
        channel->gamma = NULL;
        channel->spi_fd = 0;
        channel->spi_speed = SK9822_DEFAULT_SPI_SPEED;
        strcpy(channel->spi_dev, "/dev/spidev0.0");
        channel->raw = NULL;
    }
}

sk9822_return_t sk9822_init(sk9822_t* sk9822)
{
	int i;
	for (i = 0;i < RPI_MAX_SPI_DEVICE;i++) {
        int spi_fd;
        static uint8_t mode;
        static uint8_t bits = 8;
        
        sk9822_channel_t* channel = &sk9822->channels[i];
        if (channel->count) {
            uint32_t speed = channel->spi_speed;

            spi_fd = open(channel->spi_dev, O_RDWR);
            if (spi_fd < 0) {
                fprintf(stderr, "Cannot open %s spi_bcm2835 module not loaded?\n", channel->spi_dev);
                return SK9822_ERROR_SPI_SETUP;
            }
            channel->spi_fd = spi_fd;

            // SPI mode
            if (ioctl(spi_fd, SPI_IOC_WR_MODE, &mode) < 0)
            {
                fprintf(stderr, "Cannot set SPI_IOC_WR_MODE\n");
                return SK9822_ERROR_SPI_SETUP;
            }
            if (ioctl(spi_fd, SPI_IOC_RD_MODE, &mode) < 0)
            {
                fprintf(stderr, "Cannot set SPI_IOC_RD_MODE\n");
                return SK9822_ERROR_SPI_SETUP;
            }

            // Bits per word
            if (ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0)
            {
                fprintf(stderr, "Cannot set SPI_IOC_WR_BITS_PER_WORD\n");
                return SK9822_ERROR_SPI_SETUP;
            }
            if (ioctl(spi_fd, SPI_IOC_RD_BITS_PER_WORD, &bits) < 0)
            {
                fprintf(stderr, "Cannot set SPI_IOC_RD_BITS_PER_WORD\n");
                return SK9822_ERROR_SPI_SETUP;
            }

            // Max speed Hz
            if (ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0)
            {
                fprintf(stderr, "Cannot set SPI_IOC_WR_MAX_SPEED_HZ to %d\n", speed);
                return SK9822_ERROR_SPI_SETUP;
            }
            if (ioctl(spi_fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed) < 0)
            {
                fprintf(stderr, "Cannot set SPI_IOC_RD_MAX_SPEED_HZ to %d\n", speed);
                return SK9822_ERROR_SPI_SETUP;
            }

            if (channel->gpionum != SK9822_DEFAULT_SPI_GPIO) {
                const rpi_hw_t* rpi_hw = rpi_hw_detect();//< RPI Hardware Information
                if (!rpi_hw) {
                    return SK9822_ERROR_HW_NOT_SUPPORTED;
                }                     
                uint32_t base = rpi_hw->periph_base;
                // Set SPI-MOSI pin
                channel->gpio = mapmem(GPIO_OFFSET + base, sizeof(gpio_t), DEV_GPIOMEM);
                if (!channel->gpio)
                {
                    return SK9822_ERROR_SPI_GPIO;
                }
                gpio_function_set(channel->gpio, channel->gpionum, 0);	// SPI-MOSI ALT0
            }

            //init led buffer
            channel->leds = malloc(sizeof(sk9822_led_t) * channel->count);
            if (!channel->leds)
            {
                sk9822_fini(sk9822);
                return SK9822_ERROR_OUT_OF_MEMORY;
            }

            //init raw buffer for render data
            channel->raw_size = (channel->count + 1) /* 32*/ + ((channel->count + 15) / 16);
            channel->raw = malloc(channel->raw_size);
            if (!channel->leds)
            {
                sk9822_fini(sk9822);
                return SK9822_ERROR_OUT_OF_MEMORY;
            }

            memset(channel->leds, 0, sizeof(sk9822_led_t) * channel->count);
            memset(channel->raw, 0, channel->raw_size); //led count  + start + end frame

            if (!channel->strip_type)
            {
                channel->strip_type = SK9822_STRIP;
            }

            // Set default uncorrected gamma table
            if (!channel->gamma)
            {
                channel->gamma = malloc(sizeof(uint8_t) * 256);
                if (!channel->gamma)
                {
                    sk9822_fini(sk9822);
                    return SK9822_ERROR_OUT_OF_MEMORY;
                }
                int x;
                for (x = 0; x < 256; x++) {
                    channel->gamma[x] = x;
                }
            }

            channel->wshift = (channel->strip_type >> 24) & 0xff;
            channel->rshift = (channel->strip_type >> 16) & 0xff;
            channel->gshift = (channel->strip_type >> 8) & 0xff;
            channel->bshift = (channel->strip_type >> 0) & 0xff;
        }
	}

	return SK9822_SUCCESS;
}

void sk9822_fini(sk9822_t* sk9822)
{
    int i;
    for (i = 0;i < RPI_MAX_SPI_DEVICE;i++) {
        if (sk9822->channels[i].count) {
            free(sk9822->channels[i].leds);
            free(sk9822->channels[i].raw);
            if (sk9822->channels[i].spi_fd) close(sk9822->channels[i].spi_fd);
        }
    }
}

sk9822_return_t sk9822_render_channel(sk9822_channel_t* channel) {
    volatile uint8_t* raw = channel->raw;
    int i,j;
    uint8_t array_size = 3; // Assume 3 color LEDs, RGB

    // If our shift mask includes the highest nibble, then we have 4 LEDs, RBGW.
    /*if (channel->strip_type & SK6812_SHIFT_WMASK)
    {
        array_size = 4;
    }*/
    
    const int scale = (channel->brightness & 0xff) + 1;

    //start frame 32 0-bits
    memset((void *) raw, channel->invert ? 0xFF :  0x00 , 4);
    raw += 4;

    for (i = 0; i < channel->count; i++)                // Led
    {
        
        const int brightness = scale * ((channel->leds[i].brightness & 0xff) + 1);

        uint8_t color[] =
        {
            channel->gamma[(((channel->leds[i].color >> channel->rshift) & 0xff) * brightness) >> 16], // red
            channel->gamma[(((channel->leds[i].color >> channel->gshift) & 0xff) * brightness) >> 16], // green
            channel->gamma[(((channel->leds[i].color >> channel->bshift) & 0xff) * brightness) >> 16], // blue
            //channel->gamma[(((channel->leds[i].color >> channel->wshift) & 0xff) * brightness) >> 16], // white, not supported
        };

        *raw = channel->invert ? 0x00 : 0xFF; //first 3 bit = 111 + global brightness, set brightness always to max.
        raw++;

        for (j = 0; j < array_size; j++) // Color
        {
            *raw = channel->invert ? (~color[j]) : color[j];
            raw++;
        }
    }

    //end frame:
    
    //datasheet says end frame = 4 bytes 0xFF but this is for one LED?
    //see: https://cpldcpu.wordpress.com/2014/11/30/understanding-the-apa102-superled/
    //https://www.pololu.com/product/3087
    //https://www.pololu.com/file/0J1234/sk9822_datasheet.pdf
    memset((void*)raw, channel->invert ? 0x00 : 0xFF, ((channel->count+15) / 16));

    int ret;
    struct spi_ioc_transfer tr;



    memset(&tr, 0, sizeof(struct spi_ioc_transfer));
    tr.tx_buf = (unsigned long)channel->raw;
    tr.rx_buf = 0;
    tr.bits_per_word = 8;
    tr.len = channel->raw_size; //(channel->count * channel->color_size) + 1 + (channel->count / 8) + 3;

    ret = ioctl(channel->spi_fd, SPI_IOC_MESSAGE(1), &tr);
    if (ret < 1)
    {
        fprintf(stderr, "Can't send spi message %s, bytes: %d. ", channel->spi_dev, channel->raw_size);
        return SK9822_ERROR_SPI_TRANSFER;
    }

    return SK9822_SUCCESS;


}

//renders all channels
sk9822_return_t sk9822_render(sk9822_t* sk9822)
{
    int i;
    for (i = 0;i < RPI_MAX_SPI_DEVICE;i++) {
        sk9822_channel_t* channel = &sk9822->channels[i];
        if (channel->count) {
            sk9822_return_t res = sk9822_render_channel(channel);
            if (res != SK9822_SUCCESS) return res;
        }
    }

	return SK9822_SUCCESS;
}

const char* sk9822_get_return_t_str(const sk9822_return_t state)
{
    const int index = -state;
    static const char* const ret_state_str[] = { SK9822_RETURN_STATES(SK9822_RETURN_STATES_STRING) };

    if (index < (int)(sizeof(ret_state_str) / sizeof(ret_state_str[0])))
    {
        return ret_state_str[index];
    }

    return "";
}
