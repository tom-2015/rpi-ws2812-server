#include "change_layer.h"

//change_layer, changes the current cairo render object to a different layer
//change_layer <channel>,<layer_nr>
void change_layer(thread_context* context, char* args) {
    int channel = 0, layer_nr=0;

    args = read_channel(args, &channel);

    if (is_valid_2D_channel_number(channel)) {
        args = read_int(args, &layer_nr);
        if (debug) printf("change_layer %d, %d.\n", channel, layer_nr);
        
        channel_info * led_channel = get_channel(channel);
        if (layer_nr != 0) {
            layer_nr--;
            if (layer_nr >=0 && layer_nr < CAIRO_MAX_LAYERS && led_channel->layers[layer_nr].cr != NULL) {
                led_channel->cr = led_channel->layers[layer_nr].cr;
            } else {
                fprintf(stderr, "Invalid layer nr %d.\n", layer_nr + 1);
                return;
            }
        } else {
            led_channel->cr = led_channel->main_cr;
        }

    } else {
        fprintf(stderr, ERROR_INVALID_2D_CHANNEL);
    }
}