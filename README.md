Open JagGD: Reverse-Engineered Jaguar GameDrive Utility
=======================================================

This program is an alternate version of the jaggd.exe utility released
on the AtariAge forums by SainT, the developer of the Jaguar GameDrive. For
more information on the original program, please see this forum post:

https://atariage.com/forums/topic/307865-command-line-tools

This program attempts to emulate the behavior of jaggd.exe as closely as
possible, with a few minor improvements. The behavior of the original
program was reverse engineered for the sole purpose of enabling interopability/
compatibility with the Jaguar GameDrive product's development features on Linux
and other non-windows operating systems.

Usage:
------

Run the program without any parameters to see the usage:

    Usage: jaggd [commands]

    -r         Reboot
    -rd        Reboot to debug stub

    From stub mode (all ROM, RAM > $2000) --
    -u[x] file[,a:addr,s:size,o:offset,x:entry]
               Upload to address with size and file offset and optionally execute.
    -x addr    Execute from address

    Prefix numbers with '$' or '0x' for hex, otherwise decimal is assumed.

On Linux/Unix, the program generally must be run with root permissions, e.g.
using sudo:

    $ sudo jaggd -rd

Building:
---------

The program requires gnumake, a C compiler (Only gcc has been tested), and
libusb-1.0 and its development packages to build on Linux. On Ubuntu, you can
install everything you need by running:

    # apt-get install git build-essential libusb-1.0-0-dev

Once these are installed, just type run 'make' to build the jaggd binary.
