# Raspberry PI LED matrix server

A simple C++ program to display a slideshow of images (and GIF) on a HUB75 matrix display. Uses [hzeller/rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix) and ImageMagick.

## Prerequisites

- add `dtparam=audio=off` in `/boot/config.txt`
- remove `dtoverlay=w1-gpio` in `/boot/config.txt`
- add `blacklist snd_bcm2835` to `/etc/modprobe.d/alsa-blacklist.conf`
- `sudo apt-get install libgraphicsmagick++-dev libwebp-dev`
- `sudo apt-get remove bluez bluez-firmware pi-bluetooth triggerhappy pigpio`
- remove `pam_systemd` and `pam_chksshpwd` in `/etc/pam.d/common-session`
- install [WiringPi](https://github.com/WiringPi/WiringPi/releases)

## Usage

1. Run `./deploy.sh <ip_address>` it will copy the necessary files to your Raspberry PI, compile the program then register and start the service.

2. Use `rsync` or an FTP client to transfer your images to `/home/pi/matrix/images`, after a while (max 10 seconds) the slideshow begins.

Images can be in PNG (5s duration) and animated GIF format (respects frame delay).

For convenience, the RPI IP address is displayed on program start.

## Configuration

There are some constants at the begining of `matrix.cpp`
- `MATRIX_WIDTH` and `MATRIX_HEIGHT` : size of the screen and expected size of images
- `IMAGE_DELAY` : default duration for still images (PNG)
- `NUMS_FILE`, `NUMS_WIDTH`, `NUMS_HEIGHT` and `NUMS_SPACE` : configuration of a file containing numbers 0 to 9 + ":" symbol
- `SHOW_TIME`, `TIME_X` and `TIME_Y` : configuration of the digital clock
- `BUTTON_PIN` : input pin for a button (on/off)
