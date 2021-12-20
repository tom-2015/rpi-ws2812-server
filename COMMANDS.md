# General commands
Here is a list of commands you can type or send to the program. All commands have optional comma seperated parameters. The parameters must be in the correct order!

* `init` command must be called everytime the program is started after the setup command, this will initialize resource on the Pi according to the setup command

```
init <frequency>,<dma>

# <frequency> Frequency to use for communication to the LEDs, default 800000
# <dma>       DMA channel number to use, default 10 be careful not to use any channels in use by the system as this may crash your SD card/OS
#             use cat /proc/device-tree/soc/dma@7e007000/brcm,dma-channel-mask to see which channels (bitmask) might be used by the OS
```

* `setup` command must be called everytime the program is started:

Depending on the `<led_type>` the setup command takes different parameters.

```

# For <led_type> values 0-11 (WS2812 and SK6812 chips):

setup <channel>,<led_count>,<led_type>,<invert>,<global_brightness>,<gpionum>

# <channel>            Channel number
# <led_count>          Number of leds in channel
# <led_type>           Type of led (3 color or 4 color) default 0
# <invert>             Invert output, default 0
# <global_brightness>  Global brightness level for channel (0-255), default 255
# <gpionum>            GPIO output number, default 18 for more see 'GPIO usage' at this page: https://github.com/jgarff/rpi_ws281x


# For <led_type> values 12-17 (SK9822 chips):

setup <channel>,<led_count>,<led_type>,<invert>,<global_brightness>,<SPI_DEV>,<SPI_SPEED>,<ALT_SPI_PIN>

# <channel>            Channel number
# <led_count>          Number of leds in channel
# <led_type>           Type of led chip WS2811, SK6812, SK9822 or virtual (see below) default 0
# <invert>             Invert SPI data pin (clock cannot be inverted)
# <global_brightness>  Global brightness level for channel (0-255), default 255
# <SPI_DEV>            The spi device file to use (default /dev/spidev0.0)
# <SPI_SPEED>,         The speed of the SPI bus (default 20Mhz)
# <ALT_SPI_PIN>        Alternative SPI MOSI output pin default 10, can be set to 38


# If using 2 SK9822 led strips at /dev/spidev0.0 and /dev/spidev0.1 you need to add some logic gates to direct MOSI and SPI CLOCK to the right led string when CE0 and CE1 pins are enabled.

Possible LED types:
 0 WS2811_STRIP_RGB
 1 WS2811_STRIP_RBG
 2 WS2811_STRIP_GRB
 3 WS2811_STRIP_GBR
 4 WS2811_STRIP_BRG
 5 WS2811_STRIP_BGR
 6 SK6812_STRIP_RGBW
 7 SK6812_STRIP_RBGW
 8 SK6812_STRIP_GRBW
 9 SK6812_STRIP_GBRW
10 SK6812_STRIP_BRGW
11 SK6812_STRIP_BGRW
12 SK9822_STRIP_RGB
13 SK9822_STRIP_RBG
14 SK9822_STRIP_GRB
15 SK9822_STRIP_GBR
16 SK9822_STRIP_BRG
17 SK9822_STRIP_BGR

99 VIRTUAL LED CHANNEL
100 Slave channel, forward data to another slave connected Raspberry

When using led_type = 99 this function takes different arguments:
setup <channel>,<led_count>, 99, <parent_channel_number>, <offset>

# <channel>            		Channel number to initialize
# <led_count>		   		Number of leds in the virtual channel
# 99				   		Virtual channel LED type
# <parent_channel_number>	The phyisical channel number previously initialized with the setup command
# <offset>					Start at this LED position in the phyisical LED string

# When using LED type = 99 you can create a virtual LED channel which is a part of a physical LED channel previously setup with one of the other LED types. This enables you to connect all your LEDs together and use the effect commands as if they were connected as sperated strings.

When using led_type = 100 this function takes different arguments:
setup <channel>,<led_count>, 100, <Host/IP>, <port>, <color_count>

# <channel>            		Channel number to initialize
# <led_count>		   		Number of leds in the virtual channel
# 100				   		Master - Slave channel
# <Host/IP>	                The name or IP of the slave connected Raspberry Pi
# <port>					Port the slave is listening on (TCP)
# <color_count>             In case the slave has connected the SK6812 chip, color count must be 4 else use default 3.

# When using LED type = 100 you can create a channel that will render on a slave Raspberry Pi. All color data is transmitted in TCP packets over the network everytime you call the render function. On the remote slave Pi run the command slave_listen to listen for incoming data.

```

* `slave_listen` Listens for incoming connection from master Pi, returns when connection is lost/closed.
```
slave_listen <channel>,<port>

# <channel>   Channel number to use
# <port>      TCP port to listen for incoming connection from the master Pi
```

* `save_state` saves current color and brightness values of a channel to a CSV file, format is:
8 character hex number for color + , + 2 character hex for brightness + new line: WWBBGGRR,FF
the CSV file can be loaded with load_state command.
```
save_state <channel>,<filename>,<start>,<len>

# <channel>   Channel number to use
# <filename>  File where to save the data
# <start>     Start saving at led index (default is led 0)
# <len>       Save this number of leds (default is the entire led string)
```

* `load_state` loads saved color and brightness values from CSV file. see save_state for format.
```
load_state <channel>,<filename>,<start>,<len>

# <channel>   Channel number to load to
# <filename>  The file where to load from
# <start>     Start loading at this LED index
# <len>       Load this number of LEDs from the file
```

* `reset` reset all initialized led strings, ready for new setup commands
```
reset
```

* `render` command sends the internal buffer to all leds

```
render <channel>,<start>,<RRGGBBRRGGBB...>

# <channel>         Send the internal color buffer to all the LEDS of <channel> default is 1
# <start>           Before render change the color of led(s) beginning at <start> (0=led 1)
# <RRGGBBRRGGBB...> Color to change the led at start Red+green+blue (no default)
```

# Thread commands

* `set_thread_exit_type` This will set if the thread should be aborted when the kill_thread or ini_thread command is executed for the <thread_id> parameter
						 READY + newline (CR + LF) will be returned when the thread has exited.
```
set_thread_exit_type <thread_id>,<type>

# <thread_id>   The thread number  (1-63)
# <type>        Exit type: 0 - aborts current running thread and immediate execute next commands
                           1 - wait until all commands completed, then exit
```

* `wait_thread` wait for a given thread to finish all commands (the exit type is ignored here)
```
wait_thread <thread_id>

# <thread_id>  The thread number (1-63)
```

* `kill_thread` terminate the given thread_id
```
kill_thread <thread_id>,<type>

# <thread_id>    The thread number (1-63)
# <type> 		 Exit type: 0 - aborts current running thread
#                           1 - wait until all commands completed
```

* `wait_signal` waits for a signal from another thread before executing the next command
```
wait_signal
```


* `signal_thread` send a signal to the given thread_id to continue executing commands
```
signal_thread <thread_id>, <wait until thread is waiting for the signal>
# <thread_id>  										The thread number (1-63)
# <wait until thread is waiting for the signal>		Set to 1 to stop and wait until the thread_id reached wait_signal, best is to always put this to 1.
```

# 1D commands

* `rotate` command moves all color values of 1 channel

```
rotate <channel>,<places>,<direction>,<RRGGBB>,<brightness>

# <channel>     Channel to rotate (default 1)
# <places>      Number of places to move each color value (default 1)
# <direction>   Direction (0 or 1) for forward and backwards rotating (default 0)
# <RRGGBB>      First led(s) get this color instead of the color of the last led
# <brightness>  Brightness for new color (previous argument) 0-255
```

* `rainbow` command creates rainbows or gradient fills
```
rainbow <channel>,<count>,<start_color>,<end_color>,<start>,<len>

# <channel>     Channel to fill with a gradient/rainbow (default 1)
# <count>       Number of times to repeat the rainbow in the channel (default 1)
# <start_color> Color to start with value from 0-255 where 0 is red and 255 pink (default is 0)
# <end_color>   Color to end with value from 0-255 where 0 is red and 255 pink (default 255)
# <start>       Start at this led position
# <len>         Number of leds to change
```

* `fill` command fills number of leds with a color value
```
fill <channel>,<RRGGBB>,<start>,<len>,<OR|AND|XOR|NOT>

# <channel>        Channel to fill leds with color (default 1)
# <RRGGBB>         Color to fill (default FF0000)
# <start>          At which led should we start (default is 0)
# <len>            Number of leds to fill with the given color after start (default all leds)
# <OR,AND,XOR,NOT> Bitwise operator to execute on OLD and NEW color, default = copies new color to output
```

* `delay` command waits for number of milliseconds
```
delay <milliseconds>

# <milliseconds>  Enter number of milliseconds to wait
```

* `brightness` command changes the brightness of a single or multiple leds without changing the actual color value
```
brightness <channel>,<brightness>,<start>,<len>

# <channel>      Channel number to change brightness (default 1)
# <brightness>   Brightness to set (0-255, default 255)
# <start>        Start at this led number (default 0)
# <len>          Number of leds to change starting at start (default led count of channel)
```

* `fade` command changes the brightness over time
```
fade <channel>,<start_brightness>,<end_brightness>,<delay ms>,<step>,<start_led>,<len>

# <channel>          Channel to fade
# <start_brightness> Start brightness (default 0)
# <end_brightness>   End brightness (default 255)
# <delay ms>         Delay in ms
# <step>             Step to increase / decrease brightness every delay untill end_brightness is reached
# <start_led>        Start led
# <len>              Number of leds to change starting at start (default is channel count)
```

* `gradient` command makes a smooth change of color or brightness level in a channel
```
gradient <channel>,<RGBWL>,<start_level>,<end_level>,<start_led>,<len>

# <channel>      Channel number to change
# <RGBWL>        Which color component to change, R = red, G = green, B = blue, W = white and L = brightness level
# <start_level>  Start at color level (0-255) default is 0
# <end_level>    End at color level (0-255) default is 255
# <start_led>    Start at led number (default is 0)
# <len>          Number of leds to change (default is channel count)
```

* `random` command can create a random color
```
random <channel>,<start>,<len>,<RGBWL>

# <channel>   Channel number to change
# <start>     Start at this led
# <len>       Number of leds to fill with a random color, default is channel count
# <RGBWL>     Color to use in random can be R = red, G = green, B = blue, W = White, L = brightness also combination is possible like RGBW or RL
```

* `readjpg` command can read the pixels from a JPEG file and fill them into the LEDs of a channel
```
readjpg <channel>,<FILE>,<start>,<len>,<offset>,<OR|AND|XOR|NOT>,<delay>,<flip_rows>

# <channel>         Channel number to load pixels to
# <FILE>            File location of the JPG without any "" cannot contain a ,
# <start>           Start position, start loading at this LED in channel (default 0)
# <len>             Load this ammount of pixel/LEDs (default is channel count or led count)
# <offset>          Start at pixel offset in JPG file (default is 0)
# <OR|AND|XOR|NOT>  Operator to use, use NOT to reverse image (default is =)
# <delay>           Optional argument the delay between rendering next scan line in the jpg file, if 0 only first line is loaded in to memory and no render performed. default 0
# <flip_rows>       Optional argument to indicate to horizontally flip rows with odd index
```

* `readpng` command can read the pixels from a PNG file and fill them into the LEDs of a channel
```
readpng <channel>,<FILE>,<BACKCOLOR>,<start>,<len>,<offset>,<OR|AND|XOR>,<delay>,<flip_rows>

# <channel>,        Channel number to load pixels to
# <FILE>,           File location of the PNG file without any "" cannot contain a ,
# <BACKCOLOR>,      The color to use for background in case of a transparent image (default is the PNG image backcolor = P), if BACKCOLOR = W the alpha channel will be used for the W in RGBW LED strips
# <start>,          Start position, start loading at this LED in channel (default 0)
# <len>,            Load this ammount of pixel/LEDs	(default is channel count or led count)
# <offset>,         Start at pixel offset in JPG file (default is 0)
# <OR AND XOR =>,   Operator to use, use NOT to reverse image (default is =)
# <delay>,          Optional argument the delay between rendering next scan line in the png file, if 0 only first line is loaded in to memory and no render performed. default 0
# <flip_rows>,      Optional argument to indicate to horizontally flip rows with odd index
```

* `blink` command makes a group of leds blink between 2 given colors
```
blink <channel>,<color1>,<color2>,<delay>,<blink_count>,<startled>,<len>

# <channel>      Channel number to change
# <color1>       First color to use
# <color2>       Second color
# <delay>        Delay in ms between change from color1 to color2
# <blink_count>  Number of changes between color1 and color2
# <startled>     Start at this led position
# <len>          Number of LEDs to blink starting at startled
```

* `random_fade_in_out` creates some kind of random blinking/fading leds effect
```
random_fade_in_out <channel>,<duration Sec>,<count>,<delay>,<step>,<inc_dec>,<brightness>,<start>,<len>,<color>

# <channel>      Channel number to use
# <duration_s>   Duration of effect in seconds
# <count>        Max number of leds that will fade in or out at same time
# <delay>        Delay between changes in brightness
# <step>         Ammount of brightness to increase/decrease between delays
# <inc_dec>      Inc_dec = if 1 brightness will start at <brightness> and decrease to initial brightness of the led, else it will start low and go up
# <brightness>   Brightness to start with when blinking starts
# <start>        Start position
# <len>          Number of leds
# <color>        Color to use for blinking leds

Example of a string with 300 LEDs:
fill 1,FFFFFF;
brightness 1,0;
random_fade_in_out 1,60,50,10,15,800;
```

* `color_change` slowly change all leds from one color to another
```
color_change <channel>,<start_color>,<stop_color>,<duration>,<start>,<len>

# <channel>     Channel number to use
# <start_color> Color to start with value from 0-255 where 0 is red and 255 pink (default is 0)
# <stop_color>  Color to end with value from 0-255 where 0 is red and 254 pink (default is 255)
# <duration>    Total number of ms event should take, default is 10 seconds
# <start>       Start effect at this led position
# <len>         Number of leds to change starting at start
```

* `chaser` makes a chaser light
```
chaser <channel>,<duration>,<color>,<count>,<direction>,<delay>,<start>,<len>,<brightness>,<loops>

# <channel>     Channel number to use
# <duration>    Max number of seconds the event may take in seconds (default 10) use 0 to make chaser run forever
# <color>       Color 000000-FFFFFF to use for chasing leds
# <count>       Number of LEDs in the chaser that will light up
# <direction>   Direction 1 or 0 to indicate forward/backwards direction of movement
# <delay>       Delay between moving one pixel (milliseconds) default is 10ms
# <start>       Start effect at this led position
# <len>         Number of leds the chaser will move before starting back start led
# <brightness>  Brightness value of chasing leds (0-255) default is 255
# <loops>       Max number of loops, use 0 to loop forever / duration time
```


* `fly_in` fill entire string with given brightness level, moving leds from left/right untill all leds have brightness level or a given color
```
fly_in <channel>,<direction>,<delay>,<brightness>,<start>,<len>,<start_brightness>,<color>

# <channel>           Channel number to use
# <direction>         Direction where to start with fly in effect (default 1)
# <delay>             Delay between moving pixels in ms (default 10ms)
# <brightness>        Final brightness of all leds default 255 or full ON
# <start>             Start effect at this led position
# <len>               Number of leds to change starting at start
# <start_brightness>  At beginning give all leds this brightness value
# <color>             Final color of the leds default is to use the current color

# NOTICE: first fill entire strip with a color if leaving color argument default (use fill <channel>,<color>)
```

* `fly_out` fill entire string with given brightness level, moving leds from left/right untill all leds have brightness level or a given color
```
fly_out <channel>,<direction>,<delay>,<brightness>,<start>,<len>,<end_brightness>,<color>

# <channel>          Channel number to use
# <direction>        Direction where to start with fly out effect (default 1)
# <delay>            Delay between moving pixels in ms (default 10ms)
# <brightness>       Brightness of led that is moving out default is 255
# <start>            Start effect at this led position
# <len>              Number of leds to change starting at start
# <end_brightness>   Brightness of all leds at end, default is 0 = OFF
# <color>            Final color of the leds default is to use the current color

#NOTICE: first fill entire strip with a color before calling this function (use fill <channel>,<color>)
```

* `progress` generates a progress bar effect or sets the leds brightness to a given progress value
```
progress <channel>,<direction>,<delay>,<start>,<len>,<brightness_on>,<brightness_off>,<value>

# <channel>         Channel number to use
# <direction>       Direction of progress bar (default 1) start led = 0%, start + len = 100%
# <delay>           Delay between increments of progress (default 1s), set 0 to not automatically increment and render progress bar but use the value argument
# <start>           Start effect at this led position, default 0
# <len>             Number of leds to change starting at start, default length of strip
# <brightness_on>   Brightness of led that is on default 255
# <brightness_off>  Brightness of led that is off default 0
# <value>           Set progress to this value (delay must be 0, manual render needed)

# NOTICE: first fill entire strip with a color before calling this function (use fill <channel>,<color> or rainbow,...)
```

* `ambilight` Capture video from USB HDMI capture card or Raspberry camera and generate ambilight for your TV 
```
ambilight <channel>,<video_device>,<leds_x>,<leds_y>,<image_width>,<image_height>,<ambi_type>,<start>,<delay>

# <channel>            Channel number to use
# <video_device>       Video device file to use (in /dev), ex. /dev/video0
# <leds_x>             Number of LEDs mounted on the back of the TV horizontally (at top)
# <leds_y>             Number of LEDs mounted vertically
# <image_width>        Width of image to capture, to reduce CPU load keep as low as possible, preferred = leds_x (default 640)
# <image_height>       Height of image to capture, to reduce CPU load keep as low as possible, preferred = leds_y (default 360)
# <ambi_type>          Type of ambilight: 1 = left,right and TOP. 2 = left, right, top and bottom. Default is 1
# <start>              Start at this LED index in the string
# <delay>              Delay between capture of frames, set to 0 to get maximum frame rate. Default is 10 ms.
# NOTICE: first LED must be mounted at the back of the TV in the left bottom corner, then rest follow clockwise.
```

# 2D commands

* `config_2D` configure 2D LED matrix (requires compile with ENABLE_2D=1)

```
#NOTE: first call setup and init to configure a 1D led string with total LEDs in the 2D matrix.
#NOTE: origin (x=0,y=0) is always the left top of the 2D matrix.

config_2D <channel>,<width>,<height>,<panel_type>,<panel_size_x>,<panel_size_y>,<start_led>,<map_file>
# <channel>         Channel to configure
# <width>           Number of LEDs in x direction
# <height>			Number of LEDs in y direction
# <panel_type>		If connecting multiple A*B LED panels this defines how LEDs are connected in each panel
# <panel_size_x>	Size in x direction for each LED panel
# <panel_size_y>	Size in y direction for each LED panel
# <start_led>		2D panel starts at this position in the 1D LED string
# <map_file> 		Optional a map file defining each X,Y location and index in the 1D LED string

# panel_type values:

0 row by row and the end of each row is connected to the beginning of the next row
1 row by row but even rows are connected left to right and odd rows right to left
2 panels with LEDs configured as columns
3 panels with individual vertical rows starting left top of each panel row but odd panel rows start from the right top.
4 use the map_file which is a file having a line for each LED defining x,y=LED_INDEX
```

* `init_layer` Initializes a new 2D graphics cairo layer or change layer settings

```
init_layer <channel>,<layer_nr>,<render_operation>,<type>,<antialiasing>,<filter_type>,<x>,<y>

# <channel>				Channel number
# <layer_nr>			The layer number
# <render_operation> 	A cairo render operator, default 1 = CAIRO_OPERATOR_SOURCE, see https://www.cairographics.org/operators/
# <type>				always 0 for normal layer
# <antialiasing>		Specify the type of cairo antialiasing here
# <filter_type>			Defines the cairo filter type
# <x>					The x location of the layer
# <y>					The y location of the layer

Render operators:
CAIRO_OPERATOR_CLEAR = 0
CAIRO_OPERATOR_SOURCE = 1
CAIRO_OPERATOR_OVER = 2
CAIRO_OPERATOR_IN = 3
CAIRO_OPERATOR_OUT = 4
CAIRO_OPERATOR_ATOP = 5
CAIRO_OPERATOR_DEST = 6
CAIRO_OPERATOR_DEST_OVER = 7
CAIRO_OPERATOR_DEST_IN = 8
CAIRO_OPERATOR_DEST_OUT = 9
CAIRO_OPERATOR_DEST_ATOP = 10
CAIRO_OPERATOR_XOR = 11
CAIRO_OPERATOR_ADD = 12
CAIRO_OPERATOR_SATURATE = 13
CAIRO_OPERATOR_MULTIPLY = 14
CAIRO_OPERATOR_SCREEN = 15
CAIRO_OPERATOR_OVERLAY = 16
CAIRO_OPERATOR_DARKEN = 17
CAIRO_OPERATOR_LIGHTEN = 18
CAIRO_OPERATOR_COLOR_DODGE = 19
CAIRO_OPERATOR_COLOR_BURN = 20
CAIRO_OPERATOR_HARD_LIGHT = 21
CAIRO_OPERATOR_SOFT_LIGHT = 22
CAIRO_OPERATOR_DIFFERENCE = 23
CAIRO_OPERATOR_EXCLUSION = 24
CAIRO_OPERATOR_HSL_HUE = 25
CAIRO_OPERATOR_HSL_SATURATION = 26
CAIRO_OPERATOR_HSL_COLOR = 27
CAIRO_OPERATOR_HSL_LUMINOSITY = 28

Antialiasing types:
BEST
DEFAULT = 0
FAST
GOOD
GRAY = 2
NONE = 1
SUBPIXEL = 3

Filter types:
CAIRO_FILTER_BEST = 2
CAIRO_FILTER_BILINEAR = 4
CAIRO_FILTER_FAST = 0
CAIRO_FILTER_GOOD = 1
CAIRO_FILTER_NEAREST = 3
```

* `change_layer` changes the current layer 2D cairo graphics are painted to

```
change_layer <channel>,<layer_nr>

# <channel>			Channel number
# <layer_nr>		The layer number to use for the next 2D graphics commands
```

* `draw_circle` Draws a circle or arc (cairo_arc) on the current selected layer

```
draw_circle <channel>,<x>,<y>,<radius>,<color>,<border_width>,<border_color>,<start_angle>,<stop_angle>,<negative>

# <channel>			Channel number
# <x>				X of the circle center
# <y>				Y of the circle center
# <radius>			Circle size
# <color>			The fill color of the circle, leave empty for no fill
# <border_width>	The width of the circle border
# <border_color>    Color of the circle border
# <start_angle>		Start angle in degrees (set to 0 for full circle)
# <stop_angle>		Stop angle in degrees (set to 360 for full circle)
# <negative>		Draw a negative arc (cairo_arc_negative)
```

* `cls` Fills current layer with a color

```
# <channel>			Channel number
# <color>			Color to fill the current layer, default transparent except bottom layer: black
```

* `draw_image` draws an image file

```
draw_image <channel>,<file_name>,<dst_x>,<dst_y>,<src_x>,<src_y>,<dst_width>,<dst_height>,<src_width>,<src_height>,<speed>,<max_loops>
This will paint a JPG, PNG or (animated) GIF to the LED matrix.

# <channel>			Channel number
# <file_name>		The file name to load picture data from
# <dst_x>			Destination X co. (default 0)
# <dst_y>			Destination Y co. (default 0)
# <src_x>			Source X, if you want to only take a part of the images specify where to start (default 0)
# <src_y>			Source Y, same as source X (default 0)
# <dst_width>		Destination width (how wide the image will be painted on the channel) default the file width
# <dst_height>		Destination height, default the file height
# <src_width>		Source width, change together with src_x if you want only a part of the image file to be painted
# <src_height> 		Source height, change together with src_width
# <speed>			If the file is an animated gif, specify here a speed multiplier, for example 2 will play at twice the speed. default is 1
# <max_loops>		for animated gif only Stop repeating the animation after max_loops, default 0 = inifite loops or loops defined in the GIF
```

* `draw_line` draws an image file

```
draw_line <channel>,<x1>,<y1>,<x2>,<y2>,<width>,<color>
Draws a cairo antialiased line from x1,y1 to x2,y2 and width pixels wide. All coordinates and width can be decimal numbers.

# <channel>		Channel number
# <x1>			Start X co. of the line
# <y1>			Start Y co. of the line
# <x2>			End X co. of the line
# <y2>			End Y co. of the line
# <width>		Width in pixels of the line can be decimal number
# <color>		The line color, default is transparent
```

* `draw_sharp_line` draws an image file

```
draw_sharp_line <channel>,<x1>,<y1>,<x2>,<y2>,<width>,<color>
Draws a sharp line using Bresenham algorithm, all parameters must be integers. The width parameter is not implemented a.t.m. and must always be 1.

# <channel>		Channel number
# <x1>			Start X co. of the line
# <y1>			Start Y co. of the line
# <x2>			End X co. of the line
# <y2>			End Y co. of the line
# <width>		Width in pixels of the line can be decimal number
# <color>		The line color, default is transparent
```

* `draw_rectangle` draws and fills a rectangle

```
draw_rectangle <channel>,<x>,<y>,<width>,<height>,<color>,<border_width>,<border_color>

# <channel>			Channel number
# <x>				X location of the left top
# <y>				Y location of the left top
# <width>			Width of the rectangle
# <height>			Height of the rectangle
# <color>			Fill color of the rectangle
# <border_width>	Border width (default 1)
# <border_color>	Border color
```

* `message_board` makes a scrolling message

```
message_board <channel>,<x>,<y>,<width>,<height>,<direction>,<text_color>,<back_color>,<delay>,<loops>,<text>,<font_size>,<font_anti_alias>,<options>,<font>

# <channel>			Channel number
# <x>				X location of the left top
# <y>				Y location of the left top
# <width>			Width of the rectangle that fits the message board
# <height>			Height of the rectangle that fits the message board
# <direction>		Sets the scrolling direction
# <text_color>		Color of the text
# <back_color>		Background color (optional, default transparent and renders on top of what already exists)
# <delay>			Delay between moving the text 1 pixel in ms (default 10ms).
# <loops>			Number of times to repeat the message before command ends, 0 to loop forever.
# <text>			The text to display, if contains , character use double quotes around the text.
# <font_size>		Height of the text in pixels.
# <font_anti_alias> Set antialiasing options, default is (1, CAIRO_ANTIALIAS_NONE) set to 2 to enable antialiasing
# <options>			Sets options about the font, combination possible by using sum of options (set bits)
# <font>			Font name or font file (.ttf) location, default monospace

Directions:
MESSAGE_DIRECTION_RIGHT_LEFT = 0
MESSAGE_DIRECTION_LEFT_RIGHT = 1
MESSAGE_DIRECTION_BOTTOM_TOP = 2
MESSAGE_DIRECTION_TOP_BOTTOM = 3

options:
CAIRO_FONT_WEIGHT_BOLD: 1
CAIRO_HINT_STYLE_NONE: 2
```

* `print_text` prints text, uses cairo_show_text

```
print_text <channel>,<x>,<y>,<text>,<color>,<font_size>,<font_anti_alias>,<options>,<font>

# <channel>			Channel number
# <x>				X location of the left top
# <y>				Y location of the left top
# <text>			The text to display, if contains , character use double quotes around the text.
# <color>			The text color
# <font_size>		Height of the text in pixels.
# <font_anti_alias> Set antialiasing options, default is (1, CAIRO_ANTIALIAS_NONE) set to 2 to enable antialiasing
# <options>			Sets options about the font, combination possible by using sum of options (set bits)
# <font>			Font name or font file (.ttf) location, default monospace

options:
CAIRO_FONT_WEIGHT_BOLD: 1
CAIRO_HINT_STYLE_NONE: 2
```

* `text_input` prints scrolling text which is send to a TCP socket listening on port_nr

```
text_input <channel>,<x>,<y>,<width>,<height>,<port_nr>,<direction>,<text_color>,<back_color>,<delay>,<font_size>,<font_anti_alias>,<options>,<font>

# <channel>			Channel number
# <x>				X location of the left top
# <y>				Y location of the left top
# <width>			Width of the rectangle that fits the message board
# <height>			Height of the rectangle that fits the message board
# <port_nr>			Port number to listen on (connect with TCP and send characters to this port)
# <direction>		Sets the scrolling direction 0 right to left, 1 left to right
# <text_color>		Color of the text
# <back_color>		Background color (optional, default transparent and renders on top of what already exists)
# <delay>			Delay between moving the text 1 pixel in ms (default 10ms)
# <font_size>		Height of the text in pixels.
# <font_anti_alias> Set antialiasing options, default is (1, CAIRO_ANTIALIAS_NONE) set to 2 to enable antialiasing
# <options>			Sets options about the font, combination possible by using sum of options (set bits) see print_text
# <font>			Font name or font file (.ttf) location, default monospace
```

* `set_pixel_color` Sets a pixel to a color
```
pixel_color <channel>,<x>,<y>,<z>,<color>

# <channel>			Channel number
# <x>				X location to fill
# <y>				Y location to fill
# <z>				Z location to fill, a.t.m. Z is not supported
# <color>			Color to set in the pixel
```

* `camera` Captures frames from a v4l2 MJPEG camera (Raspberry Pi camera or HDMI USB capture card). Prints this to the 2D matrix.
```
camera <channel>,<camera_device>,<dst_x>,<dst_y>,<dst_width>,<dst_height>,<frame_count>,<delay>

# <channel>			Channel number
# <camera_device>	Video device file for camera in /dev, for ex. /dev/video0
# <dst_x>			Destination x location (left top) where the frame will be printed on the display (default 0)
# <y>				Destination y location (left top) where the frame will be printed on the display (default 0)
# <dst_width>		Destination width of the frame (default matrix width)
# <dst_height>      Destination height of the frame (default matrix height)
# <frame_count>     Capture and display this ammount of frames then terminate the command
# <delay>           Delay in ms between each frame (default 10ms)
```

# Audio commands

* `record_audio` records audio

```
record_audio <device>,<sample_rate>,<sample_count>,<channels>

# <device>			Audio device, use arecord -L to list device and use the "plughw:*" device.
# <sample_rate>		Here you can change the number of samples per second, default is 24000. Higher will require more CPU but better results with high frequency.
# <sample_count>	Number of samples to store in the internal buffer before forwarding to DSP. Default is 1024 and you better leave it like that.
# <channels>		Number of audio channels, default is 2 (stereo)
```

Since version 6.2 you can use udp port or named pipe as audio input device.
For example: udp://9000 as device will listen on local port 9000. External program can then send raw audio samples to this port.
Audio samples can also be broadcasted on the network to multiple Pis when using udpb://9000 it will listen to broadcast port 9000.
Example: arecord and socat can be used for this (also possible on a remote device by changing the destination ip 127.0.0.1):
```
arecord -f FLOAT_LE -r 24000 -c 2 - | socat -b 1024 - udp-datagram:127.0.0.1:6000,broadcast
```

Another option is to create a local named pipe with pipe:///dev/audio_input this will create a file where you can send audio samples to.
For example with arecord:
```
arecord -D "plughw:CARD=Capture,DEV=0" -V stereo -f FLOAT_LE -r 24000 -c 2 - > /dev/audio_input
```

* `light_organ` Generates a light organ, alle LEDs blink to the rhythm of the music.

```
light_organ <channel>,<color_mode>,<color(s)>,<color_change_delay>,<duration>,<delay>,<start>,<len>

# <channel>				Channel number
# <color_mode>			Sets how changing of color should behave (default 2).
# <color(s)>			Give color or multiple colors like FF0000 (red) or FF000000FF000000FF (red,green,blue)
# <color_change_delay>	Number of seconds to wait before changing the color
# <duration>			Number of seconds before automatic exit of the command, 0 to run forever.
# <delay>				Delay between rendering of the strip. default is 25ms increase to make flashing slower
# <start>				Start at this LED number
# <len>					Effect for this number of LEDs starting at start
```
Color modes:  
0 = 1 fixed color  
1 = color sequence provided in the color(s) argument  
2 = random colors provided in the color(s) argument, color(s) argument can be left empty for some default colors.  
3 = keep existing color in the strip, this must be set first  

* `pulses` Generates wave pattern on the LED strip (like a music driven chaser)

```
pulses <channel>,<threshold>,<color_mode>,<color(s)>,<delay>,<color_change_delay>,<direction>,<duration>,<min_brightness>,<start>,<len>

# <channel>				Channel number
# <threshold>			Minimum level of audio sample to react, float value range 0-1 default is 0.1, audio sample values <0.1 will be ignored
# <color_mode>			Sets how changing of color should behave, see light_organ for possible values (default 2).
# <color(s)>			Give color or multiple colors like FF0000 (red) or FF000000FF000000FF (red,green,blue)
# <delay>				Delay between rendering of the strip, default 25ms. Decrease to make wave go faster but increase CPU load
# <color_change_delay>	Number of seconds to wait before changing the color
# <direction>			Chaser go forward or backwards
# <duration>			Number of seconds before automatic exit of the command, 0 to run forever.
# <min_brightness>		Minimum level of brightness or LEDs will be turned off (default is 10 max 255).
# <start>				Start at this LED number
# <len>					Effect for this number of LEDs starting at start
```

* `vu_meter` 

```
vu_meter <channel>,<color_mode>,<color(s)>,<color_change_delay>,<duration>,<delay>,<brightness>,<start>,<len>

# <channel>				Channel number
# <color_mode>			Sets how changing of color should behave (default 4).
# <color(s)>			Give color or multiple colors like FF0000 (red) or FF000000FF000000FF (red,green,blue)
# <color_change_delay>	Number of seconds to wait before changing the color
# <duration>			Number of seconds before automatic exit of the command, 0 to run forever.
# <delay>				Delay between rendering of the strip, default 25ms. Decrease to make wave go faster but increase CPU load
# <brightness>			Brightness level of turned on LEDs, (0-255) default is 255
# <start>				Start at this LED number
# <len>					Effect for this number of LEDs starting at start
```
Color modes:  
0 = 1 fixed color  
1 = color sequence provided in the color(s) argument  
2 = random colors provided in the color(s) argument, color(s) argument can be left empty for some default colors.  
3 = keep existing color in the strip, this must be set first   
4 = Classic VU meter colors (green - yellow - orange - red)  