#include "line.h"
#include <math.h>
//draw_line
//draw_line <channel>,<x1>,<y1>,<x2>,<y2>,<width>,<color>
void draw_line(thread_context* context, char* args) {
    int channel = 0, color = COLOR_TRANSPARENT, height = 0;
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0, width = 0;
    bool color_empty = true;

    args = read_channel(args, &channel);

    if (is_valid_2D_channel_number(channel)) {

        args = read_double(args, &x1);
        args = read_double(args, &y1);
        args = read_double(args, &x2);
        args = read_double(args, &y2);
        args = read_double(args, &width);
        args = read_color_arg_empty(args, &color, 4, &color_empty);

        //cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);

        if (debug) printf("draw_line %d,%f,%f,%f,%f,%f,%d\n", channel, x1, y1, x2, y2, width, color);

        channel_info * led_channel = get_channel(channel);
        cairo_t* cr = led_channel->cr;

        if (!color_empty) {
            set_cairo_color_rgba(cr, color);
        }

        cairo_new_path(cr);
        cairo_set_line_width(cr, width);

        cairo_move_to(cr, x1, y1);
        cairo_line_to(cr, x2, y2);
        cairo_stroke(cr);
    } else {
        fprintf(stderr, ERROR_INVALID_2D_CHANNEL);
    }
}

//draw a sharp 1 pixel line without anti aliasing
//draw_sharp_line <channel>,<x1>,<y1>,<x2>,<y2>,<width>,<color>
//width argument must always be 1, not implemented
void draw_sharp_line(thread_context* context, char* args) {
    int x, y, dx, dy, dx1, dy1, px, py, xe, ye, i;
    int x1, y1, x2, y2, channel, line_width=1;
    unsigned int color = 255;

    args = read_channel(args, &channel);

    if (is_valid_2D_channel_number(channel)) {

        args = read_int(args, &x1);
        args = read_int(args, &y1);
        args = read_int(args, &x2);
        args = read_int(args, &y2);
        args = read_int(args, &line_width);
        args = read_color_arg(args, &color, 3);

    } else {
        fprintf(stderr, ERROR_INVALID_2D_CHANNEL);
        return;
    }

    if (debug) printf("draw_sharp_line %d,%d,%d,%d,%d,%d\n", channel, x1, y1, x2, y2, color);

    channel_info * led_channel = get_channel(channel);
    cairo_surface_flush(led_channel->surface);
    unsigned int* pixels = (unsigned int*)cairo_image_surface_get_data(led_channel->surface); //get pointer to pixel data
    unsigned int pixel_stride = led_channel->pixel_stride;
    int width = led_channel->width;
    int height = led_channel->height;

    color = convert_cairo_color(color);

    dx = x2 - x1;
    dy = y2 - y1;
    dx1 = fabs(dx);
    dy1 = fabs(dy);
    px = 2 * dy1 - dx1;
    py = 2 * dx1 - dy1;
    if (dy1 <= dx1)
    {
        if (dx >= 0)
        {
            x = x1;
            y = y1;
            xe = x2;
        }
        else
        {
            x = x2;
            y = y2;
            xe = x1;
        }
        if (y < height && x < width) pixels[y * pixel_stride + x] = color;
        for (i = 0;x < xe;i++)
        {
            x = x + 1;
            if (px < 0)
            {
                px = px + 2 * dy1;
            }
            else
            {
                if ((dx < 0 && dy < 0) || (dx > 0 && dy > 0))
                {
                    y = y + 1;
                }
                else
                {
                    y = y - 1;
                }
                px = px + 2 * (dy1 - dx1);
            }
            if (y < height && x < width) pixels[y * pixel_stride + x] = color;
        }
    }
    else
    {
        if (dy >= 0)
        {
            x = x1;
            y = y1;
            ye = y2;
        }
        else
        {
            x = x2;
            y = y2;
            ye = y1;
        }
        if (y < height && x < width) pixels[y * pixel_stride + x] = color;
        for (i = 0;y < ye;i++)
        {
            y = y + 1;
            if (py <= 0)
            {
                py = py + 2 * dx1;
            }
            else
            {
                if ((dx < 0 && dy < 0) || (dx > 0 && dy > 0))
                {
                    x = x + 1;
                }
                else
                {
                    x = x - 1;
                }
                py = py + 2 * (dx1 - dy1);
            }
            if (y < height && x < width) pixels[y * pixel_stride + x] = color;
        }
    }

    cairo_surface_mark_dirty_rectangle(led_channel->surface, (x1 > x2) ? x2 : x1, (y1 > y2) ? y2 : y1, dx, dy);

}