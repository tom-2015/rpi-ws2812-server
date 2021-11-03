
#include "init_layer.h"
//init_layer, inits an extra layer or changes painting options, layers are painted from top to bottom
//init_layer <channel>,<layer_nr>,<render_operation>,<type>,<antialiasing>,<filter_type>,<x>,<y>
//filter_types:
/*CAIRO_FILTER_BEST = 2
CAIRO_FILTER_BILINEAR = 4
CAIRO_FILTER_FAST = 0
CAIRO_FILTER_GOOD = 1
CAIRO_FILTER_NEAREST = 3*/
/*
* 
    BEST
    DEFAULT = 0
    FAST
    GOOD
    GRAY = 2
    NONE = 1
    SUBPIXEL = 3
* */

/*
* 
CAIRO_OPERATOR_CLEAR = 0
CAIRO_OPERATOR_SOURCE = 1
CAIRO_OPERATOR_OVER = 2
CAIRO_OPERATOR_IN = 3
CAIRO_OPERATOR_OUT = 4
CAIRO_OPERATOR_ATOP = 5
CAIRO_OPERATOR_DEST = 6
CAIRO_OPERATOR_DEST_OVER = 7
CAIRO_OPERATOR_DEST_IN = 8
CAIRO_OPERATOR_DEST_OUT = 9
CAIRO_OPERATOR_DEST_ATOP = 10
CAIRO_OPERATOR_XOR = 11
CAIRO_OPERATOR_ADD = 12
CAIRO_OPERATOR_SATURATE = 13
CAIRO_OPERATOR_MULTIPLY = 14
CAIRO_OPERATOR_SCREEN = 15
CAIRO_OPERATOR_OVERLAY = 16
CAIRO_OPERATOR_DARKEN = 17
CAIRO_OPERATOR_LIGHTEN = 18
CAIRO_OPERATOR_COLOR_DODGE = 19
CAIRO_OPERATOR_COLOR_BURN = 20
CAIRO_OPERATOR_HARD_LIGHT = 21
CAIRO_OPERATOR_SOFT_LIGHT = 22
CAIRO_OPERATOR_DIFFERENCE = 23
CAIRO_OPERATOR_EXCLUSION = 24
CAIRO_OPERATOR_HSL_HUE = 25
CAIRO_OPERATOR_HSL_SATURATION = 26
CAIRO_OPERATOR_HSL_COLOR = 27
CAIRO_OPERATOR_HSL_LUMINOSITY = 28
*/

void init_layer(thread_context* context, char* args) {
    int channel = 0, layer_nr = 0, x=0, y=0, type=0, operation= CAIRO_OPERATOR_SOURCE, antialias=CAIRO_ANTIALIAS_DEFAULT, filter_type=CAIRO_FILTER_FAST;

    args = read_channel(args, &channel);

    if (is_valid_2D_channel_number(channel)) {

        args = read_int(args, &layer_nr);
        args = read_int(args, &operation);
        args = read_int(args, &type);
        args = read_int(args, &antialias);
        args = read_int(args, &filter_type);
        args = read_int(args, &x);
        args = read_int(args, &y);

        if (debug) printf("init_layer %d,%d,%d,%d,%d,%d.\n", channel, layer_nr, operation, type, x, y);
        
        //cairo_pattern_set_filter(cairo_get_source(led_channel->cr), CAIRO_FILTER_NEAREST);
        //cairo_set_antialias(led_channel->cr, CAIRO_ANTIALIAS_NONE);
        channel_info * led_channel = get_channel(channel);

        if (layer_nr == 0) {
            cairo_set_operator(led_channel->cr, operation);
            cairo_pattern_set_filter(cairo_get_source(led_channel->cr), filter_type);
            cairo_set_antialias(led_channel->cr, antialias);
        } else {
            layer_nr--;
            if (layer_nr >= 0 && layer_nr < CAIRO_MAX_LAYERS) {
                if (led_channel->layers[layer_nr].cr == NULL) {
                    led_channel->layers[layer_nr].surface = cairo_surface_create_similar(led_channel->surface, CAIRO_CONTENT_COLOR_ALPHA, led_channel->width, led_channel->height);
                    led_channel->layers[layer_nr].cr = cairo_create(led_channel->layers[layer_nr].surface);
                }
                led_channel->layers[layer_nr].type = type;
                led_channel->layers[layer_nr].op = operation;
                led_channel->layers[layer_nr].x = x;
                led_channel->layers[layer_nr].y = y;

                cairo_pattern_set_filter(cairo_get_source(led_channel->layers[layer_nr].cr), filter_type);
                cairo_set_antialias(led_channel->layers[layer_nr].cr, antialias);
            } else {
                fprintf(stderr, "Invalid layer nr %d.\n", layer_nr + 1);
                return;
            }
        }
    } else {
        fprintf(stderr, ERROR_INVALID_2D_CHANNEL);
    }
}