#include "cls.h"
//fills 2D channel with color
//cls <channel>,<color>
void cls(thread_context* context, char* args) {
    int channel = 0, color = COLOR_TRANSPARENT;

    args = read_channel(args, &channel);

    if (is_valid_2D_channel_number(channel)) {

        channel_info * led_channel = get_channel(channel);
        cairo_t* cr = led_channel->cr;

        if (cr == led_channel->main_cr) color = 0;

        args = read_color_arg(args, &color, 4);

        cairo_save(cr);
        cairo_identity_matrix(cr);
        set_cairo_color_rgba(cr, color);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);

        cairo_restore(cr);

        /*cairo_surface_flush(led_channel->surface);
        if (color == 0) {
            unsigned char* pixels = cairo_image_surface_get_data(led_channel->surface); //get pointer to pixel data
            memset(pixels, convert_cairo_color(color), led_channel->surface_stride * sizeof(unsigned char) * led_channel->height);
        }else {
            unsigned int * pixels = (unsigned int *) cairo_image_surface_get_data(led_channel->surface); //get pointer to pixel data
            unsigned int* pixels_end = pixels + led_channel->width * led_channel->height;
            color = convert_cairo_color(color);
           while (pixels != pixels_end) {
                *pixels = color;
                pixels++;
            }
        }

        cairo_surface_mark_dirty(led_channel->surface);*/
    } else {
        fprintf(stderr, ERROR_INVALID_2D_CHANNEL);
    }
}