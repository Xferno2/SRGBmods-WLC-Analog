## This is a prototype code for Non Adressable LED Strips to work with SRGBmods-WLC
- This code supports 1 LED Strip connected to GPIO 22,21,20 (r,g,b variables)
- For Raspberry Pico W
- This code is just to adapt your own implementation to support Non Adressable LED
- To be able to use multiple trips https://srgbmods.net/wifilc/ create a new firware from the website and specify more leds to the first strip. Then adjust the for loops to add 3 more pins (r, g, b) for the other strip.
- It can work for both Adressable and Non Adressable LED Strips if you uncomment the code for the Adreesabele ones (leds.setPixelColor) and make a separate loop for the Non Adressable ones.
- The Non Adressable Strips must always have 1 LED in configuration.
- Hardware Lightning doesn't work yet.
- For how to connect the analog LED Strip to the Raspberry as an example watch here https://www.youtube.com/watch?v=WtP7sxWSF9c
