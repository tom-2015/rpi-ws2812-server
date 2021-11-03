#define _USE_MATH_DEFINES
#include <math.h>
#include "../ws2812svr.h"

//http://zetcode.com/gfx/cairo/basicdrawing/
//draw_circle
//draw_circle <channel>,<x>,<y>,<radius>,<color>,<border_width>,<border_color>,<start_angle>,<stop_angle>,<negative>
void draw_circle(thread_context* context, char* args) {
    int channel = 0, fill_color = COLOR_TRANSPARENT, border_color = COLOR_TRANSPARENT;
    double start_angle = 0.0, stop_angle = 360.0 , x = 0, y = 0, radius = 0, border_width = 0;
    bool fill_color_empty = true;
    int negative = 0;

    args = read_channel(args, &channel);

    if (is_valid_2D_channel_number(channel)) {

        args = read_double(args, &x);
        args = read_double(args, &y);
        args = read_double(args, &radius);
        args = read_color_arg_empty(args, &fill_color, 4, &fill_color_empty);
        args = read_double(args, &border_width);
        args = read_color_arg(args, &border_color, 4);
        args = read_double(args, &start_angle);
        args = read_double(args, &stop_angle);
        args = read_int(args, &negative);

        //cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
        if (debug) printf("draw_circle %d,%f,%f,%f,%d,%f,%d,%f,%f,%d\n", channel, x, y, radius, fill_color, border_width, border_color,start_angle,stop_angle,negative);

        channel_info * led_channel = get_channel(channel);
        cairo_t* cr = led_channel->cr;
       

        /*cairo_set_line_width(cr, 1);
        set_cairo_color_rgba(cr, 255);
        cairo_translate(cr, 4, 4);
        cairo_arc(cr, 0, 0, 3, 0, 2.0 * M_PI);
        cairo_close_path(cr);
        cairo_stroke_preserve(cr);
        set_cairo_color_rgba(cr, 0xFF00);
        cairo_fill(cr);*/

        cairo_save(cr);
        

        if (border_width > 0.0) {
            cairo_set_line_width(cr, border_width);
            set_cairo_color_rgba(cr, border_color);
        }

        cairo_translate(cr, x, y);
        
        if (negative==0) {            
            cairo_arc(cr, 0, 0, radius, start_angle / 360.0 * 2.0 * M_PI, stop_angle / 360.0 * 2.0 * M_PI);
        } else {
            cairo_arc_negative(cr, 0, 0, radius, start_angle / 360.0 * 2.0 * M_PI, stop_angle / 360.0 * 2.0 * M_PI);
        }

        if (border_width > 0) {
            if (!fill_color_empty) {
                cairo_close_path(cr);
                cairo_stroke_preserve(cr);
            } else {
                cairo_stroke(cr);
            }
        }

        if (!fill_color_empty) {
            set_cairo_color_rgba(cr, fill_color);
            cairo_fill(cr);
        }

        cairo_restore(cr);

    }   else {
        fprintf(stderr, ERROR_INVALID_2D_CHANNEL);
    }
}