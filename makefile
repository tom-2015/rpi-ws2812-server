
all: ws2812.elf

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

ws2812.elf: dma.o ws2811.o pwm.o main.o
	$(CC) $(LINK) $^ -o $@

clean:
	rm *.o

#all: release testbank.elf
#gcc file.c -I/usr/include -L/usr/lib -L/usr/local/lib -I/usr/lib/arm-linux-gnueabihf -lusb -o file.elf
#$@ before :
#$^ after  :
#$< first after :
#rule: dependencies
#http://www.cs.colby.edu/maxwell/courses/tutorials/maketutor/
#g++ main.cpp frm_gtm.cpp frm_main.cpp frm_bb.cpp sockets/PracticalSocket.cpp auto_version_listener.cpp -Wfatal-errors -mfpu=neon -mfloat-abi=softfp -mcpu=cortex-a8 -fomit-frame-pointer -O3 -fno-math-errno -fno-signed-zeros -fno-tree-vectorize -export-dynamic $(INCL) -lm -lpthread -Xlinker -L/usr/lib -Xlinker $(LINK) -o testbank.elf 