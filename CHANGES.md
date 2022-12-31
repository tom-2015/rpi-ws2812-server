
# Version 7.4
* Bug fixes with virtual channels
* Improve audio signal processing
* Add support to stream audio samples to multiple threads
* Fix bugs in documentation
* Python code for the Raspberry Pi Pico W to operate as a slave network channel.

# Version 7.3
* Add new command "wave" as alternative to "pulses", it renders a low pass audio wave directly to the strip.

# Version 7.2
* Add new hardware version for Raspberry Pi 4 and Raspberry Pi Zero.

# Version 7.1
* Fix cairo crash after sending to many characters in the text_input command.

# Version 7.0
* render command will now render ALL channels if no channel argument is used or 0 as channel number is used (for backwards compatibility)

# Version 6.9
* Add new command wait_ready_signal.

# Version 6.8
* make read_png function work with multiple threads at same time
* all brightness arguments now expect number 0-255

# Version 6.7
* Fix bug with SPI rendering for SK9822

# Version 6.5
* Add function to render video from camera directly on a 2D panel
* Add function to generate ambilight effect from camera/HDMI capture input
* Improve code by generating header files and splitting files
* Remove the need to enable audio or 2D graphics at compile time
* Support for Raspberry Pi Zero 2 W
* Update documentation

# Version 6.3
* Fixed bug with SK6812 and 2D panels
* Update documentation
 
# Version 6.2
* Added support for audio packets through UDP/named pipe

# Version 6.1
* Added functions to make LEDs react to audio input

# Version 6.0
* Added support for 2D graphics
* Added virtual channels

# Version 5.0
* Fix flickering when using multiple threads and 2xws2811 led strings

# Version 5.0
* support for new Raspberry Pi 4 revisions
* support added for SK9822 chip

# Version 4.0

* now possible to run multiple threads in the background executing different commands at the same time.
* threads can be started from all input methods (TCP, CLI, named pipe).
* added thread synchronization and termination commands.
* new progress bar effect (progress command).
* readpng, readjpg support for horizontal flip of odd rows.
* no need to enter a value for optional commands, for example: progress 1,1,0,,,,,1 will use default values for arguments in ,,,,,
* do ... loop supported from direct console input (CLI).