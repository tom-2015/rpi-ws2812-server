
#include "rectangle.h"
//draw_rectangle
//draw_rectangle <channel>,<x>,<y>,<width>,<height>,<color>,<border_width>,<border_color>
void draw_rectangle(thread_context* context, char* args) {
    int channel = 0, fill_color = COLOR_TRANSPARENT, border_color= COLOR_TRANSPARENT;
    double x = 0.0, y = 0.0, width = 10.0, height = 10.0, border_width = 1.0;
    bool fill_color_empty = true;

    args = read_channel(args, &channel);

    if (is_valid_2D_channel_number(channel)) {

        args = read_double(args, &x);
        args = read_double(args, &y);
        args = read_double(args, &width);
        args = read_double(args, &height);
        args = read_color_arg_empty(args, &fill_color, 4, &fill_color_empty);
        args = read_double(args, &border_width);
        args = read_color_arg(args, &border_color, 4);
        //cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);

        if (debug) printf("draw_rectangle %d,%d,%d,%d,%d,%d,%d,%d\n", channel, x, y, width, height, color, border_width, border_color);

        channel_info * led_channel = get_channel(channel);
        cairo_t* cr = led_channel->cr;

        if (border_width > 0.0) {
            cairo_set_line_width(cr, border_width);
            set_cairo_color_rgba(cr, border_color);
        }

        cairo_rectangle(cr, x, y, width, height);

        if (border_width > 0.0) {
            if (!fill_color_empty) cairo_stroke_preserve(cr);
            else cairo_stroke(cr);
        }

        if (!fill_color_empty) {
            set_cairo_color_rgba(cr, fill_color);
            cairo_fill(cr);
        }

        cairo_fill(cr);

    } else {
        fprintf(stderr, ERROR_INVALID_2D_CHANNEL);
    }
}