all: ws2812svr

#pkg-config --libs --cflags cairo
#-I/usr/include/glib-2.0 -I/usr/lib/arm-linux-gnueabihf/glib-2.0/include -I/usr/include/pixman-1 -I/usr/include/uuid -I/usr/include/freetype2 -I/usr/include/libpng16 
#pkg-config --libs --cflags cairo x11

INCL=-I/usr/include
LINK=-L/usr/lib -L/usr/local/lib -I/usr/lib/arm-linux-gnueabihf -pthread -lm
CC=gcc -g $(INCL)

ifneq (1,$(NO_PNG))
  CC += -DUSE_PNG
  LINK += -lpng
endif

ifneq (1,$(NO_JPEG))
  CC += -DUSE_JPEG
  LINK += -ljpeg
endif

ifeq (1,$(ENABLE_2D))
  INCL += `pkg-config --cflags cairo x11 xcb freetype2`
  CC += -DUSE_2D
  LINK += `pkg-config --libs cairo x11 xcb freetype2`
endif

ifeq (1,$(ENABLE_AUDIO))
  INCL += `pkg-config --cflags alsa`
  CC += -DUSE_AUDIO
  LINK += `pkg-config --libs alsa`
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

ifneq (1,$(NO_PNG))
readpng.o: readpng.c readpng.h
	$(CC) -c $< -o $@
endif

gifdec.o: gifdec.c gifdec.h
	$(CC) -c $< -o $@
	
ws2811.o: ws2811.c ws2811.h rpihw.h pwm.h pcm.h mailbox.h clk.h gpio.h dma.h rpihw.h readpng.h
	$(CC) -c $< -o $@

sk9822.o: sk9822.c sk9822.h ws2811.h mailbox.h
	$(CC) -c $< -o $@

main.o: main.c ws2811.h sk9822.o ws2811.o gifdec.o effects/rotate.c effects/rainbow.c effects/fill.c effects/brightness.c effects/fade.c effects/blink.c effects/gradient.c effects/add_random.c effects/random_fade_in_out.c effects/chaser.c effects/color_change.c effects/fly_in.c effects/fly_out.c effects/read_png.c effects/read_jpg.c effects/progress.c 2D/set_pixel.c 2D/screenshot.c 2D/cls.c 2D/print_text.c 2D/message_board.c 2D/circle.c 2D/line.c 2D/rectangle.c 2D/image.c 2D/change_layer.c 2D/init_layer.c 2D/text_input.c audio/pulses.c audio/record.c audio/light_organ.c audio/vu_meter.c
	$(CC) -c $< -o $@

ifneq (1,$(NO_PNG))
ws2812svr: main.o dma.o mailbox.o pwm.o pcm.o ws2811.o sk9822.o rpihw.o readpng.o gifdec.o
	$(CC) $(LINK) $^ -o $@
else
ws2812svr: main.o dma.o mailbox.o pwm.o pcm.o ws2811.o sk9822.o rpihw.o gifdec.o
	$(CC) $(LINK) $^ -o $@
endif

clean:
	rm *.o
	
install: ws2812svr
	-systemctl stop ws2812svr.service
	cp ws2812svr.service  /etc/systemd/system/ws2812svr.service
	cp -n ws2812svr.conf /etc/ws2812svr.conf
	cp ws2812svr /usr/local/bin
	systemctl daemon-reload
	-systemctl stop ws2812svr.service
	systemctl enable ws2812svr.service
	systemctl start ws2812svr.service
	