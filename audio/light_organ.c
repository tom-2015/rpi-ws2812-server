//light_organ <channel>,<color_mode>,<color(s)>,<color_change_delay>,<duration>,<delay>,<start>,<len>
void light_organ(thread_context* context, char* args) {
	const unsigned int MAX_COLORS = 255;
	int channel = 0;
	int duration = 0;
	int delay = 25;
	int len = 0, start = 0;
	unsigned int next_color = 255;
	unsigned int colors[MAX_COLORS];
	int color_count = 0;
	int color_mode = 2; //default some random colors
	int color_change_delay = 60; //60 sec change color
	float gain = 4.0;
	int i;

	if (!context->audio_capture.capturing) {
		fprintf(stderr, "Error audio capture is not running on this thread. Use the audio_capture command to start capturing first!\n");
		return;
	}

	args = read_channel(args, &channel);

	if (is_valid_channel_number(channel)) {
		//args = read_float(args, &threshold);
		args = read_int(args, &color_mode);

		switch (color_mode) {
		case 0: //fixed color
			args = read_color_arg(args, &next_color, get_color_size(channel));
			colors[0] = next_color;
			break;
		case 1: //color sequence
			if (*args == ',') args++;
			while (color_count < MAX_COLORS && *args != ',' && *args != 0) {
				args = read_color(args, &colors[color_count], get_color_size(channel));
				color_count++;
			}
			next_color = colors[0];
			break;
		case 2: //random colors
			if (*args == ',') args++;
			while (color_count < MAX_COLORS && *args != ',' && *args != 0) {
				args = read_color(args, &colors[color_count], get_color_size(channel));
				color_count++;
			}
			if (color_count == 0) {
				colors[0] = color(255, 0, 0);
				colors[1] = color(255, 255, 0);
				colors[2] = color(0, 255, 255);
				colors[3] = color(0, 255, 0);
				colors[4] = color(255, 128, 0);
				colors[5] = color(255, 0, 128);
				colors[6] = color(0, 0, 255);
				color_count = 7;
			}
			next_color = colors[0];
			break;
		case 3: //don't care about color

			break;
		default:
			fprintf(stderr, "Error invalid color mode %d\n", color_mode);
			return;
			break;
		}

		args = read_int(args, &color_change_delay);
		args = read_int(args, &duration);
		args = read_int(args, &delay);
		args = read_int(args, &start);
		args = read_int(args, &len);

		if (debug) printf("light_organ %d,%d,%d,%d,%d,%d,%d,%d\n", channel, color_mode, color_count, color_change_delay, duration, delay, start, len);

		if (start >= get_led_count(channel)) start = 0;
		if ((start + len) > get_led_count(channel)) len = get_led_count(channel) - start;
		if (len == 0) len = get_led_count(channel);

		ws2811_led_t* leds = get_led_string(channel);
		unsigned int start_time = time(0);
		unsigned int color_time = time(0);
		unsigned int next_color_time = color_time;
		unsigned int color_index = 0;
		unsigned int current_color = next_color;

		volatile float low_pass_buffer=0; //here the record function will store the number of times threshold was reached
		context->audio_capture.capture_dst_buffer = (void*)&low_pass_buffer;
		context->audio_capture.dsp_mode = DSP_MODE_LOW_PASS;

		while ((((time(0) - start_time) < duration) || duration == 0) && context->end_current_command == 0) {
			float low_pass_value;

			pthread_mutex_lock(&context->audio_capture.buffer_mutex);
			low_pass_value = low_pass_buffer;
			pthread_mutex_unlock(&context->audio_capture.buffer_mutex);

			unsigned int intensity = 255.0 * low_pass_value * gain;

			if (color_mode!=3 && next_color_time <= time(0)) {
				switch (color_mode) {
				case 0: //fixed color
					break;
				case 1: //color sequence
					next_color = colors[color_index];
					color_index++;
					if (color_index == color_count) color_index = 0;
					break;
				case 2: //random color from color list
					color_index = (float)color_count * ((float)rand() / (float)RAND_MAX);
					next_color = colors[color_index];
					break;
				}
				next_color_time = time(0) + color_change_delay;
				for (i = 0;i < len;i++) {
					leds[start + i].color = next_color;
				}
			}

			for (i = 0;i < len;i++) {
				leds[start + i].brightness = intensity;
			}

			render_channel(channel);
			usleep(delay * 1000);

		}
		context->audio_capture.dsp_mode = DSP_MODE_NONE;
	}
	else {
		fprintf(stderr, ERROR_INVALID_CHANNEL);
	}
}