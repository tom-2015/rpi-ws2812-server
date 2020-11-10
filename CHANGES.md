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