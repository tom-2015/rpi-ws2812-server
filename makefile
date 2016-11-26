all: ws2812svr

INCL=-I/usr/include
LINK=-L/usr/lib -L/usr/local/lib -I/usr/lib/arm-linux-gnueabihf -lpthread
CC=gcc -g $(INCL) 

dma.o: dma.c dma.h
	$(CC) -c $< -o $@

mailbox.o: mailbox.c mailbox.h
	$(CC) -c $< -o $@
	
pwm.o: pwm.c pwm.h ws2811.h
	$(CC) -c $< -o $@

rpihw.o: rpihw.c rpihw.h
	$(CC) -c $< -o $@
	
ws2811.o: ws2811.c ws2811.h rpihw.h pwm.h mailbox.h clk.h gpio.h dma.h rpihw.h
	$(CC) -c $< -o $@

main.o: main.c ws2811.h
	$(CC) -c $< -o $@

ws2812svr: main.o dma.o mailbox.o pwm.o ws2811.o rpihw.o
	$(CC) $(LINK) $^ -o $@
	
clean:
	rm *.o