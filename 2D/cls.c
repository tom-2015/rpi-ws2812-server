//fills 2D channel with color
//cls <channel>,<color>
void cls(thread_context* context, char* args) {
    int channel = 0, color = COLOR_TRANSPARENT;

    args = read_channel(args, &channel);

    if (is_valid_2D_channel_number(channel)) {


        cairo_t* cr = led_channels[channel].cr;

        if (cr == led_channels[channel].main_cr) color = 0;

        args = read_color_arg(args, &color, 4);

        cairo_save(cr);
        cairo_identity_matrix(cr);
        set_cairo_color_rgba(cr, color);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);

        cairo_restore(cr);

        /*cairo_surface_flush(led_channels[channel].surface);
        if (color == 0) {
            unsigned char* pixels = cairo_image_surface_get_data(led_channels[channel].surface); //get pointer to pixel data
            memset(pixels, convert_cairo_color(color), led_channels[channel].surface_stride * sizeof(unsigned char) * led_channels[channel].height);
        }else {
            unsigned int * pixels = (unsigned int *) cairo_image_surface_get_data(led_channels[channel].surface); //get pointer to pixel data
            unsigned int* pixels_end = pixels + led_channels[channel].width * led_channels[channel].height;
            color = convert_cairo_color(color);
           while (pixels != pixels_end) {
                *pixels = color;
                pixels++;
            }
        }

        cairo_surface_mark_dirty(led_channels[channel].surface);*/
    } else {
        fprintf(stderr, ERROR_INVALID_2D_CHANNEL);
    }
}