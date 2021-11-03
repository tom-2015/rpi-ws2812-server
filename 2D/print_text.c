#include "print_text.h"
//prints text at x,y
//pixel_color <channel>,<x>,<y>,<text>,<color>,<font_size>,<font_anti_alias>,<options>,<font>,<operator>
void print_text(thread_context* context, char* args) {
    int channel = 0, x = 0, y = 0, color=0, font_size=8,font_anti_alias = CAIRO_ANTIALIAS_NONE, options = 0, cairo_op = CAIRO_OPERATOR_SOURCE;
    char text[MAX_VAL_LEN] = { 0 };
    char font[MAX_VAL_LEN] = { 0 };

    args = read_channel(args, &channel);


    if (is_valid_2D_channel_number(channel)) {

        args = read_int(args, &x);
        args = read_int(args, &y);
        args = read_str(args, text, sizeof(text));
        args = read_color_arg(args, &color, 4);
        args = read_int(args, &font_size);
        args = read_int(args, &font_anti_alias);
        args = read_int(args, &options);
        args = read_str(args, font, sizeof(font));
        args = read_int(args, &cairo_op);

        if (font[0] == 0) strcpy(font, DEFAULT_FONT);

        if (debug) printf("print text %d,%d,%d,%s,%d,%d,%d,%d,%s,%d\n", channel, x, y, text, color, font_size, font_anti_alias, options, font, cairo_op);

        channel_info * led_channel = get_channel(channel);
        cairo_t* cr = led_channel->cr;

        cairo_save(cr);
        cairo_set_operator(cr, cairo_op);
        set_cairo_color_rgba(cr, color);
        
        cairo_font_options_t* font_options = cairo_font_options_create();
        cairo_font_options_set_antialias(font_options, font_anti_alias); //CAIRO_ANTIALIAS_NONE

        FT_Face ft_face;
        bool using_freetype_lib = false;
        cairo_font_face_t* cairo_ft_face;

        if (file_exists(font)) { //check if file exists and load from file
            
            if (!init_ft_lib(context)) {
                cairo_restore(cr);
                return;
            }

            FT_Error status = FT_New_Face(context->ft_lib, font, 0, &ft_face);
            if (status != 0) {
                fprintf(stderr, "Error %d opening font %s.\n", status, font);
                cairo_restore(cr);
                return;
            }
            cairo_ft_face = cairo_ft_font_face_create_for_ft_face(ft_face, 0);

            cairo_set_font_face(cr, cairo_ft_face);
            using_freetype_lib = true;

            if (debug) printf("using free type file %s.\n", font);

        } else {
            if (options & 1) cairo_select_font_face(cr, font, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            else cairo_select_font_face(cr, font, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            if (options & 2) cairo_font_options_set_hint_style(font_options, CAIRO_HINT_STYLE_NONE);

            if (debug) printf("Cairo font family %s.\n", font);
        }

        /*status = FT_Init_FreeType(&value);
        if (status != 0) {
            fprintf(stderr, "Error %d opening library.\n", status);
        }
        status = FT_New_Face(value, font, 0, &face);
        if (status != 0) {
            fprintf(stderr, "Error %d opening %s.\n", status, font);
        }
        ct = cairo_ft_font_face_create_for_ft_face(face, 0);*/




        cairo_set_font_options(cr, font_options);

        cairo_set_font_size(cr, font_size);
        
        cairo_move_to(cr, x, y);
        cairo_show_text(cr, text);
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

        cairo_restore(cr);

    } else {
        fprintf(stderr, ERROR_INVALID_2D_CHANNEL);
    }
}