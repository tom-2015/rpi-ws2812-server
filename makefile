all: ws2812svr

INCL=-I/usr/include
LINK=-L/usr/lib -L/usr/local/lib -I/usr/lib/arm-linux-gnueabihf -lpthread
CC=gcc -g $(INCL)

ifneq (1,$(NO_PNG))
  CC += -DUSE_PNG
  LINK += -lpng
endif

ifneq (1,$(NO_JPEG))
  CC += -DUSE_JPEG
  LINK += -ljpeg
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

ws2811.o: ws2811.c ws2811.h rpihw.h pwm.h pcm.h mailbox.h clk.h gpio.h dma.h rpihw.h readpng.h
	$(CC) -c $< -o $@

main.o: main.c ws2811.h effects/rotate.c effects/rainbow.c effects/fill.c effects/brightness.c effects/fade.c effects/blink.c effects/gradient.c effects/add_random.c effects/random_fade_in_out.c effects/chaser.c effects/color_change.c effects/fly_in.c effects/fly_out.c effects/read_png.c effects/read_jpg.c effects/progress.c
	$(CC) -c $< -o $@

ifneq (1,$(NO_PNG))
ws2812svr: main.o dma.o mailbox.o pwm.o pcm.o ws2811.o rpihw.o readpng.o
	$(CC) $(LINK) $^ -o $@
else
ws2812svr: main.o dma.o mailbox.o pwm.o pcm.o ws2811.o rpihw.o
	$(CC) $(LINK) $^ -o $@
endif

clean:
	rm *.o
	
install: ws2812svr
	systemctl stop ws2812svr.service
	cp ws2812svr.service  /etc/systemd/system/ws2812svr.service
	cp -n ws2812svr.conf /etc/ws2812svr.conf
	cp ws2812svr /usr/local/bin
	systemctl daemon-reload
	-systemctl stop ws2812svr.service
	systemctl enable ws2812svr.service
	systemctl start ws2812svr.service
	