#include "message_board.h"
#define MESSAGE_DIRECTION_RIGHT_LEFT 0
#define MESSAGE_DIRECTION_LEFT_RIGHT 1
#define MESSAGE_DIRECTION_BOTTOM_TOP 2
#define MESSAGE_DIRECTION_TOP_BOTTOM 3

//easy scrolling message board
//message_board <channel>,<x>,<y>,<width>,<height>,<direction>,<text_color>,<back_color>,<delay>,<loops>,<text>,<font_size>,<font_anti_alias>,<options>,<font>
void message_board(thread_context* context, char* args) {
    int channel = 0, x = 0, y = 0, text_color = 0, back_color= COLOR_TRANSPARENT, delay=10, loops=0, font_size = 8, direction= MESSAGE_DIRECTION_RIGHT_LEFT, width=0, height=0, font_anti_alias= CAIRO_ANTIALIAS_NONE, options=0;
    char text[MAX_VAL_LEN] = { 0 };
    char font[MAX_VAL_LEN] = { 0 };

    args = read_channel(args, &channel);

    if (is_valid_2D_channel_number(channel)) {

        args = read_int(args, &x);
        args = read_int(args, &y);
        args = read_int(args, &width);
        args = read_int(args, &height);
        args = read_int(args, &direction);
        args = read_color_arg(args, &text_color, 4);
        args = read_color_arg(args, &back_color, 4);
        args = read_int(args, &delay);
        args = read_int(args, &loops);
        args = read_str(args, text, sizeof(text));
        args = read_int(args, &font_size);
        args = read_int(args, &font_anti_alias);
        args = read_int(args, &options);
        args = read_str(args, font, sizeof(font));

        //cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);

        if (debug) printf("message board %d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s,%d,%d,%d,%s\n", channel, x, y, width, height, direction, text_color, back_color, delay, loops, text, font_size, font_anti_alias, options, font);

        if (font[0] == 0) strcpy(font, DEFAULT_FONT);//"enhanced_led_board-7.ttf");

        channel_info * led_channel = get_channel(channel);
        cairo_t* cr = led_channel->cr;
        
        cairo_save(cr);

        cairo_font_options_t* font_options = cairo_font_options_create();
        cairo_font_options_set_antialias(font_options, font_anti_alias); //CAIRO_ANTIALIAS_NONE
        
        FT_Face ft_face;
        bool using_freetype_lib = false;
        cairo_font_face_t* cairo_ft_face;

        if (file_exists(font)) { //check if file exists and load from file

            if (!init_ft_lib(context)) return;

            FT_Error status = FT_New_Face(context->ft_lib, font, 0, &ft_face);
            if (status != 0) {
                fprintf(stderr, "Error %d opening font %s.\n", status, font);
                return;
            }
            cairo_ft_face = cairo_ft_font_face_create_for_ft_face(ft_face, 0);

            cairo_set_font_face(cr, cairo_ft_face);
            using_freetype_lib = true;

            if (debug) printf("using free type file %s.\n", font);

        }
        else {
            if (options & 1) cairo_select_font_face(cr, font, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            else cairo_select_font_face(cr, font, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            if (options & 2) cairo_font_options_set_hint_style(font_options, CAIRO_HINT_STYLE_NONE);

            if (debug) printf("Cairo font family %s.\n", font);
        }

        cairo_set_font_options(cr, font_options);
        cairo_set_font_size(cr, font_size);

        int total_width, total_height;

        cairo_text_extents_t extents;
        cairo_text_extents(cr, text, &extents);

        total_width = extents.x_advance;
        total_height = extents.height;
        int avg_char_width = extents.x_advance / strlen(text);
        int mask_x = x, mask_y = y + extents.y_bearing;
        int p_x, p_y; //x y where to paint text next

        switch (direction) {
        case MESSAGE_DIRECTION_RIGHT_LEFT:
            if (width == 0) width = led_channel->width;
            if (height == 0) height = total_height;
            p_x = x + width + avg_char_width * 2;
            p_y = y;
            break;
        case MESSAGE_DIRECTION_LEFT_RIGHT:
            if (width == 0) width = led_channel->width;
            if (height == 0) height = total_height;
            p_x = x - total_width - 1;
            p_y = y;
            break;
        case MESSAGE_DIRECTION_BOTTOM_TOP:
            if (width == 0) width = total_width;
            if (height == 0) height = led_channel->height;
            p_x = x;
            p_y = y + total_height;
            break;
        case MESSAGE_DIRECTION_TOP_BOTTOM:
            if (width == 0) width = total_width;
            if (height == 0) height = led_channel->height;
            p_x = x;
            p_y = y - total_height;
            break;
        }
        
        int i = loops;
        while ((i > 0 || loops == 0) && !context->end_current_command) {
            
            cairo_save(cr);

            //redraw background
            //cairo_paint_layers(&led_channels[channel]);
            
            //set_cairo_color_rgb(cr, color);
            //cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            //cairo_paint(cr);

            //paint backcolor
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            set_cairo_color_rgba(cr, back_color);
            cairo_rectangle(cr, mask_x, mask_y, width, height);
            cairo_fill(cr);

            //set clipping rectangle
            //http://zetcode.com/gfx/cairo/clippingmasking/
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_rectangle(cr, mask_x, mask_y, width, height);
            cairo_clip(cr);
        
            //set forecolor
            set_cairo_color_rgba(cr, text_color);
            //cairo_select_font_face(cr, font, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);//CAIRO_FONT_WEIGHT_BOLD);
            //cairo_set_font_size(cr, font_size);
            //p_x = 2;

            cairo_move_to(cr, (double) p_x, (double) p_y);
            cairo_show_text(cr, text);

            //printf("render text: %d,%d,%d,%d,%d,%d,%s,%f,%d,%d,%d,%d\n", p_x, p_y, back_color, text_color, font_size, delay, text, extents.y_bearing, width, height, mask_x, mask_y);
            cairo_restore(cr);

            render_channel(channel);
            

            switch (direction) {
                case MESSAGE_DIRECTION_RIGHT_LEFT:
                    p_x--;
                    if (p_x < (total_width - x) * -1) {
                        p_x = x + led_channel->width + avg_char_width;
                        i--;
                    }
                    break;
                case MESSAGE_DIRECTION_LEFT_RIGHT:
                    p_x++;
                    if (p_x > (width + x)) {
                        p_x = x - total_width - 1;
                        i--;
                    }
                    break;
                case MESSAGE_DIRECTION_BOTTOM_TOP:
                    p_y--;
                    if (p_y < (y - total_height)) {
                        p_y = y + total_height;
                        i--;
                    }
                    break;
                case MESSAGE_DIRECTION_TOP_BOTTOM:
                    p_y++;
                    if (p_y > (y + total_height)) {
                        p_y = y - total_height;
                        i--;
                    }
                    break;
            }

            if (delay == 0) break;
            usleep(delay * 1000);
        }

        cairo_font_options_destroy(font_options);

        if (using_freetype_lib) {
            //https://www.cairographics.org/manual/cairo-FreeType-Fonts.html#cairo-ft-font-face-create-for-ft-face
            static const cairo_user_data_key_t key;
            cairo_status_t status = cairo_font_face_set_user_data(cairo_ft_face, &key, ft_face, (cairo_destroy_func_t)FT_Done_Face);

            if (status) {
                cairo_font_face_destroy(cairo_ft_face);
                FT_Done_Face(ft_face);
            }
        }

       // cairo_restore(cr);
    } else {
        fprintf(stderr, ERROR_INVALID_2D_CHANNEL);
    }
}