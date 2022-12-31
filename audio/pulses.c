#include "pulses.h"
#include "record.h"
#include <pthread.h>
//audio_pulses <channel>,<threshold>,<color_mode>,<color>,<delay>,<color_change_delay>,<direction>,<duration>,<min_brightness>,<start>,<len>
void audio_pulses(thread_context* context, char* args) {
	const unsigned int MAX_COLORS = 255;
	int channel=0;
	int duration = 0;
	int delay = 25;
	int len = 0, start = 0;
	int direction = 0;
	int min_brightness = 10;
	unsigned int next_color = 255;
	unsigned int colors[MAX_COLORS];
	int color_count = 0;
	int color_mode = 2; //default some random colors
	int color_change_count = 200; //5 sec with delay = 25
	int color_change_delay = 5;
	float threshold=0.1;

	if (!context->audio_capture.capturing) {
		fprintf(stderr, "Error audio capture is not running on this thread. Use the record_audio command to start capturing first!\n");
		return;
	}

	args = read_channel(args, &channel);

	if (is_valid_channel_number(channel)) {
		args = read_float(args, &threshold);
		args = read_int(args, &color_mode);

		switch (color_mode) {
		case 0: //fixed color
			args = read_color_arg(args, &next_color, get_color_size(channel));
			colors[0] = next_color;
			break;
		case 1: //color sequence
			if (*args == ',') args++;
			while (color_count < MAX_COLORS && *args!=',' && *args!=0) {
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
		case 3: //don't care about colors
			break;
		default:
			fprintf(stderr, "Error invalid color mode %d\n", color_mode);
			return;
			break;
		}

		args = read_int(args, &delay);
		args = read_int(args, &color_change_delay);
		args = read_int(args, &direction);
		args = read_int(args, &duration);
		args = read_int(args, &min_brightness);
		args = read_int(args, &start);
		args = read_int(args, &len);

		if (debug) printf("audio_pulses %d,%f,%d,%d (%d),%d,%d,%d,%d,%d,%d,%d\n", channel, threshold, color_mode, next_color, color_count, delay, color_change_delay, direction, duration, min_brightness, start, len);

		if (start >= get_led_count(channel)) start = 0;
		if ((start + len) > get_led_count(channel)) len = get_led_count(channel) - start;
		if (len == 0) len = get_led_count(channel);

		color_change_count = color_change_delay * 1000 / delay;

		context->audio_capture.threshold = threshold;

		ws2811_led_t* leds = get_led_string(channel);
		unsigned int start_time = time(0);
		unsigned int loop_count = 0;
		unsigned int color_index = 0;
		unsigned int current_color = next_color;

		volatile unsigned int threshold_reached_buffer=0; //here the record function will store the number of times threshold was reached
		context->audio_capture.capture_dst_buffer = (void*)&threshold_reached_buffer;
		context->audio_capture.dsp_mode = DSP_MODE_THRESHOLD;

		while ((((time(0) - start_time) < duration) || duration == 0) && context->end_current_command == 0) {
			unsigned int threshold_reached;

			pthread_mutex_lock(&context->audio_capture.buffer_mutex);
			threshold_reached = threshold_reached_buffer;
			threshold_reached_buffer = 0;
			pthread_mutex_unlock(&context->audio_capture.buffer_mutex);

			unsigned int intensity = 255 * threshold_reached / (context->audio_capture.channel_count * context->audio_capture.dsp_buffer_sample_count);

			if (intensity < min_brightness) intensity = 0;

			//if (debug) printf("intensity: %d\n", intensity);

			//move all leds 1 position
			unsigned int n;
			if (direction) {
				for (n = 0;n < len - 1;n++) {
					if (color_mode != 3) leds[start + n].color = leds[start + n + 1].color;
					leds[start + n].brightness = leds[start + n + 1].brightness;
				}
			} else {
				for (n = len - 1; n > 0; n--) {
					if (color_mode != 3) leds[start + n].color = leds[start + n - 1].color;
					leds[start + n].brightness = leds[start + n - 1].brightness;
				}
			}

			//set first or last led to new intensity
			if (direction) {
				if (color_mode != 3) leds[start + len - 1].color = next_color;
				leds[start + len - 1].brightness = intensity;
			} else {
				if (color_mode != 3) leds[start].color = next_color;
				leds[start].brightness = intensity;
			}

			render_channel(channel);
			usleep(delay * 1000);

			if (loop_count >= color_change_count) {
				switch (color_mode) {
				case 1:
					next_color = colors[color_index];
					color_index++;
					if (color_index == color_count) color_index = 0;
					break;
				case 2:
					color_index = (float)color_count * ((float)rand() / (float)RAND_MAX);
					next_color = colors[color_index];
					break;
				case 3: //don't care
				case 0:
					break;
				}
				loop_count = 0;
			}

			loop_count++;
		}
		context->audio_capture.dsp_mode = DSP_MODE_NONE;
	} else {
		fprintf(stderr, ERROR_INVALID_CHANNEL);
	}
}