
#ifndef __SK9822_H__
#define __SK9822_H__

#define RPI_MAX_SPI_DEVICE 16
#define SK9822_MAX_CHANNELS RPI_MAX_SPI_DEVICE

//https://www.raspberrypi.org/documentation/hardware/raspberrypi/spi/README.md

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <limits.h>
#include "ws2811.h"

// 3 color R, G and B ordering
#define SK9822_STRIP_RGB                         0x00100800
#define SK9822_STRIP_RBG                         0x00100008
#define SK9822_STRIP_GRB                         0x00081000
#define SK9822_STRIP_GBR                         0x00080010
#define SK9822_STRIP_BRG                         0x00001008
#define SK9822_STRIP_BGR                         0x00000810

// predefined fixed LED types
#define SK9822_STRIP                             SK9822_STRIP_BGR

#define SK9822_RETURN_STATES(X)                                                             \
            X(0, SK9822_SUCCESS, "Success"),                                                \
            X(-1, SK9822_ERROR_GENERIC, "Generic failure"),                                 \
            X(-2, SK9822_ERROR_OUT_OF_MEMORY, "Out of memory"),                             \
            X(-11, SK9822_ERROR_ILLEGAL_GPIO, "Selected GPIO not possible"),                \
            X(-13, SK9822_ERROR_SPI_SETUP, "Unable to initialize SPI"),                     \
            X(-14, SK9822_ERROR_SPI_TRANSFER, "SPI transfer error"),                        \
            X(-15, SK9822_ERROR_HW_NOT_SUPPORTED, "Hardware version not supported"),        \
            X(-16, SK9822_ERROR_SPI_GPIO, "Cannot use wanted SPI GPIO pin")

#define SK9822_RETURN_STATES_ENUM(state, name, str) name = state
#define SK9822_RETURN_STATES_STRING(state, name, str) str

#define SK9822_DEFAULT_SPI_GPIO -1
#define SK9822_DEFAULT_SPI_SPEED 20000000 //20MHz clock speed

typedef enum {
    SK9822_RETURN_STATES(SK9822_RETURN_STATES_ENUM),

    SK9822_RETURN_STATE_COUNT
} sk9822_return_t;


/*typedef struct
{
    uint32_t color;
    uint32_t brightness;
} sk9822_led_t;*/

#define sk9822_led_t ws2811_led_t

typedef struct
{
    int color_size;           //3 = RGB, 4 = RGBW
    int gpionum;              //alt GPIO configuration, DEFAULT_SPI_GPIO for default
    int invert;               //< Invert output signal
    int count;                //< Number of LEDs, 0 if channel is unused
    int strip_type;           //< Strip color layout
    sk9822_led_t* leds;       //< LED buffers, allocated by driver based on count
    uint8_t brightness;       //< Brightness value between 0 and 255
    uint8_t wshift;           //< White shift value
    uint8_t rshift;           //< Red shift value
    uint8_t gshift;           //< Green shift value
    uint8_t bshift;           //< Blue shift value
    uint8_t* gamma;           //< Gamma correction table
    int spi_fd;               //SPI file
    uint32_t spi_speed;       //SPI clock speed
    char spi_dev[PATH_MAX];   //SPI device file name
    uint8_t* raw;             //raw SPI data
    int raw_size;             //num bytes in raw buffer
    void* gpio;               //pointer to GPIO pin if not default
}sk9822_channel_t;

typedef struct
{
    sk9822_channel_t channels[RPI_MAX_SPI_DEVICE];
} sk9822_t;

void create_sk9822(sk9822_t* sk9822);
sk9822_return_t sk9822_init(sk9822_t* sk9822);
void sk9822_fini(sk9822_t* sk9822);
sk9822_return_t sk9822_render_channel(sk9822_channel_t* channel);
sk9822_return_t sk9822_render(sk9822_t* ws2811);
const char* sk9822_get_return_t_str(const sk9822_return_t state);

#ifdef __cplusplus
}
#endif

#endif