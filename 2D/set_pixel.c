#include "set_pixel.h"
//sets a pixel at x,y location
//pixel_color <channel>,<x>,<y>,<z>,<color>
void set_pixel_color(thread_context* context, char* args) {
    int channel = 0, x = 0, y = 0, z = 0, color = 255;

    args = read_channel(args, &channel);
    args = read_int(args, &x);
    args = read_int(args, &y);
    args = read_int(args, &z);
    


    if (is_valid_2D_channel_number(channel)) {

        args = read_color_arg(args, &color, 4);
        if (debug) printf("set_pixel_color %d, %d, %d, %d, %d\n", channel, x, y, z, color);
        channel_info * led_channel = get_channel(channel);

        if (led_channel->surface != NULL) {
            cairo_surface_flush(led_channel->surface);
            unsigned int * pixels = (unsigned int*)cairo_image_surface_get_data(led_channel->surface); //get pointer to pixel data
            pixels[y * led_channel->pixel_stride + x] = convert_cairo_color((unsigned int) color);
            cairo_surface_mark_dirty_rectangle(led_channel->surface, x, y, 1, 1);
        } else {
            ws2811_led_t*** led_string = get_2D_led_string(channel);
            if (led_string!=NULL && led_string[y][x]!=NULL) led_string[y][x]->color = color;
        }
    } else {
        fprintf(stderr, ERROR_INVALID_2D_CHANNEL);
    }
}