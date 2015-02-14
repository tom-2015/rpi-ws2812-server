all: ws2812svr

INCL=-I/usr/include
LINK=-L/usr/lib -L/usr/local/lib -I/usr/lib/arm-linux-gnueabihf -lpthread
CC=gcc  $(INCL) 

dma.o: dma.c dma.h
	$(CC) -c $< -o $@

ws2811.o: ws2811.c ws2811.h
	$(CC) -c $< -o $@

pwm.o: pwm.c pwm.h ws2811.h
	$(CC) -c $< -o $@
	
main.o: main.c gpio.h clk.h dma.o ws2811.o
	$(CC) -c $< -o $@

ws2812svr: dma.o ws2811.o pwm.o main.o
	$(CC) $(LINK) $^ -o $@

clean:
	rm *.o