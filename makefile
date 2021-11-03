all: ws2812svr

#pkg-config --libs --cflags cairo
#-I/usr/include/glib-2.0 -I/usr/lib/arm-linux-gnueabihf/glib-2.0/include -I/usr/include/pixman-1 -I/usr/include/uuid -I/usr/include/freetype2 -I/usr/include/libpng16 
#pkg-config --libs --cflags cairo x11

INCL=-I/usr/include `pkg-config --cflags cairo x11 xcb freetype2 alsa`
LINK=-L/usr/lib -L/usr/local/lib -I/usr/lib/arm-linux-gnueabihf -pthread -lm -lpng -ljpeg `pkg-config --libs cairo x11 xcb freetype2 alsa`
CC=gcc -g $(INCL)

ifneq (1,$(DEBUG))
  CC += -O3
  LINK += -lpng
endif

dma.o: dma.c dma.h
	$(CC) -c $< -o $@

mailbox.o: mailbox.c mailbox.h
	$(CC) -c $< -o $@
	
pwm.o: pwm.c pwm.h ws2811.h
	$(CC) -c $< -o $@
	
pcm.o: pcm.c pcm.h
	$(CC) -c $< -o $@

rpihw.o: rpihw.c rpihw.h
	$(CC) -c $< -o $@

readpng.o: readpng.c readpng.h
	$(CC) -c $< -o $@

gifdec.o: gifdec.c gifdec.h
	$(CC) -c $< -o $@

2D/camera.o: 2D/camera.c 2D/camera.h ws2812svr.h
	$(CC) -c $< -o $@

2D/change_layer.o: 2D/change_layer.c 2D/change_layer.h ws2812svr.h
	$(CC) -c $< -o $@

2D/circle.o: 2D/circle.c 2D/circle.h ws2812svr.h
	$(CC) -c $< -o $@

2D/cls.o: 2D/cls.c 2D/cls.h ws2812svr.h
	$(CC) -c $< -o $@

2D/image.o: 2D/image.c 2D/image.h ws2812svr.h
	$(CC) -c $< -o $@

2D/init_layer.o: 2D/init_layer.c 2D/init_layer.h ws2812svr.h
	$(CC) -c $< -o $@

2D/line.o: 2D/line.c 2D/line.h ws2812svr.h
	$(CC) -c $< -o $@

2D/message_board.o: 2D/message_board.c 2D/message_board.h ws2812svr.h
	$(CC) -c $< -o $@

2D/print_text.o: 2D/print_text.c 2D/print_text.h ws2812svr.h
	$(CC) -c $< -o $@

2D/rectangle.o: 2D/rectangle.c 2D/rectangle.h ws2812svr.h
	$(CC) -c $< -o $@

2D/screenshot.o: 2D/screenshot.c 2D/screenshot.h ws2812svr.h
	$(CC) -c $< -o $@

2D/set_pixel.o: 2D/set_pixel.c 2D/set_pixel.h ws2812svr.h
	$(CC) -c $< -o $@

2D/text_input.o: 2D/text_input.c 2D/text_input.h ws2812svr.h sockets.o
	$(CC) -c $< -o $@

audio/light_organ.o: audio/light_organ.c audio/light_organ.h ws2812svr.h
	$(CC) -c $< -o $@
	
audio/pulses.o: audio/pulses.c audio/pulses.h ws2812svr.h
	$(CC) -c $< -o $@

audio/record.o: audio/record.c audio/record.h ws2812svr.h sockets.o
	$(CC) -c $< -o $@

audio/vu_meter.o: audio/vu_meter.c audio/vu_meter.h ws2812svr.h
	$(CC) -c $< -o $@

effects/add_random.o: effects/add_random.c effects/add_random.h ws2812svr.h
	$(CC) -c $< -o $@

effects/ambilight.o: effects/ambilight.c effects/ambilight.h ws2812svr.h
	$(CC) -c $< -o $@

effects/blink.o: effects/blink.c effects/blink.h ws2812svr.h
	$(CC) -c $< -o $@

effects/brightness.o: effects/brightness.c effects/brightness.h ws2812svr.h
	$(CC) -c $< -o $@

effects/chaser.o: effects/chaser.c effects/chaser.h ws2812svr.h
	$(CC) -c $< -o $@

effects/color_change.o: effects/color_change.c effects/color_change.h ws2812svr.h
	$(CC) -c $< -o $@

effects/fade.o: effects/fade.c effects/fade.h ws2812svr.h
	$(CC) -c $< -o $@

effects/fill.o: effects/fill.c effects/fill.h ws2812svr.h
	$(CC) -c $< -o $@

effects/fly_in.o: effects/fly_in.c effects/fly_in.h ws2812svr.h
	$(CC) -c $< -o $@

effects/fly_out.o: effects/fly_out.c effects/fly_out.h ws2812svr.h
	$(CC) -c $< -o $@

effects/gradient.o: effects/gradient.c effects/gradient.h ws2812svr.h
	$(CC) -c $< -o $@

effects/progress.o: effects/progress.c effects/progress.h ws2812svr.h
	$(CC) -c $< -o $@

effects/rainbow.o: effects/rainbow.c effects/rainbow.h ws2812svr.h
	$(CC) -c $< -o $@

effects/random_fade_in_out.o: effects/random_fade_in_out.c effects/random_fade_in_out.h ws2812svr.h
	$(CC) -c $< -o $@

effects/read_jpg.o: effects/read_jpg.c effects/read_jpg.h ws2812svr.h
	$(CC) -c $< -o $@

effects/read_png.o: effects/read_png.c effects/read_png.h ws2812svr.h
	$(CC) -c $< -o $@

effects/rotate.o: effects/rotate.c effects/rotate.h ws2812svr.h
	$(CC) -c $< -o $@

sockets.o: sockets.c sockets.h
	$(CC) -c $< -o $@

master_slave.o: master_slave.c master_slave.h sockets.o
	$(CC) -c $< -o $@

ws2811.o: ws2811.c ws2811.h rpihw.h pwm.h pcm.h mailbox.h clk.h gpio.h dma.h rpihw.h readpng.h
	$(CC) -c $< -o $@

sk9822.o: sk9822.c sk9822.h ws2811.h mailbox.h
	$(CC) -c $< -o $@

jpghelper.o: jpghelper.c jpghelper.h
	$(CC) -c $< -o $@

libws2812svr.o: ws2812svr.c ws2812svr.h jpghelper.o sockets.o master_slave.o sk9822.o ws2811.o gifdec.o effects/rotate.o effects/rainbow.o effects/fill.o effects/brightness.o effects/fade.o effects/blink.o effects/gradient.o effects/add_random.o effects/random_fade_in_out.o effects/chaser.o effects/color_change.o effects/fly_in.o effects/fly_out.o effects/read_png.o effects/read_jpg.o effects/progress.o 2D/set_pixel.o 2D/screenshot.o 2D/cls.o 2D/print_text.o 2D/message_board.o 2D/circle.o 2D/line.o 2D/rectangle.o 2D/image.o 2D/change_layer.o 2D/init_layer.o 2D/text_input.o 2D/camera.o audio/pulses.o audio/record.o audio/light_organ.o audio/vu_meter.o effects/ambilight.o
	$(CC) -c $< -o $@

main.o: main.c ws2812svr.h libws2812svr.o
	$(CC) -c $< -o $@

ws2812svr: main.o dma.o mailbox.o pwm.o pcm.o ws2811.o sk9822.o rpihw.o readpng.o gifdec.o master_slave.o jpghelper.o libws2812svr.o sockets.o sk9822.o ws2811.o gifdec.o effects/rotate.o effects/rainbow.o effects/fill.o effects/brightness.o effects/fade.o effects/blink.o effects/gradient.o effects/add_random.o effects/random_fade_in_out.o effects/chaser.o effects/color_change.o effects/fly_in.o effects/fly_out.o effects/read_png.o effects/read_jpg.o effects/progress.o 2D/set_pixel.o 2D/screenshot.o 2D/cls.o 2D/print_text.o 2D/message_board.o 2D/circle.o 2D/line.o 2D/rectangle.o 2D/image.o 2D/change_layer.o 2D/init_layer.o 2D/text_input.o 2D/camera.o audio/pulses.o audio/record.o audio/light_organ.o audio/vu_meter.o effects/ambilight.o main.o
	$(CC) $(LINK) $^ -o $@

clean:
	-rm *.o
	-rm effects/*.o
	-rm 2D/*.o
	-rm audio/*.o
	
install: ws2812svr
	-systemctl stop ws2812svr.service
	cp ws2812svr.service  /etc/systemd/system/ws2812svr.service
	cp -n ws2812svr.conf /etc/ws2812svr.conf
	cp ws2812svr /usr/local/bin
	systemctl daemon-reload
	-systemctl stop ws2812svr.service
	systemctl enable ws2812svr.service
	systemctl start ws2812svr.service
	