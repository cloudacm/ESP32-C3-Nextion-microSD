The hardware used here is the Nextion HSD035383B4 which connects to a ESP32-C3 module. In addition, a SPI micro-SD module was attached to provide data logging.

<img width="1430" height="1002" alt="Nextion_ESP32-C3_microSD" src="https://github.com/user-attachments/assets/053b0d2c-4386-41c7-9757-a0944dc30287" />


The Nextion display uses proprietary software and serial formatting. This will require a Windows system with the Nextion Editor software installed.

This post (https://nextion.tech/2021/05/31/the-sunday-blog-developing-and-debugging-how-can-i-step-by-step/) provided the basis of the demo provided here. The real challenge was getting the font sizing and positioning to match what had been done on the CYD.

<img width="2016" height="1134" alt="Nextion Development" src="https://github.com/user-attachments/assets/fa7d78db-1873-42cf-8015-293820d32f46" />


The display offers some offset in development where much of the workload is handled by the editor. Programming the display can be done serially or by uploading the compiled flash file to a micro-SD card attached directly to the display.
