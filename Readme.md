The hardware used here is the Nextion HSD035383B4 which connects to a ESP32-C3 module. In addition, a SPI micro-SD module was attached to provide data logging.

https://www.cloudacm.com/wp-content/uploads/2026/07/Nextion_ESP32-C3_microSD-768x538.png

The Nextion display uses proprietary software and serial formatting. This will require a Windows system with the Nextion Editor software installed.

This post (https://nextion.tech/2021/05/31/the-sunday-blog-developing-and-debugging-how-can-i-step-by-step/) provided the basis of the demo provided here. The real challenge was getting the font sizing and positioning to match what had been done on the CYD.

https://www.cloudacm.com/wp-content/uploads/2026/07/Nextion-Development-768x432.jpg

The display offers some offset in development where much of the workload is handled by the editor. Programming the display can be done serially or by uploading the compiled flash file to a micro-SD card attached directly to the display.
