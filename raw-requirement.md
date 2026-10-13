I want to build a psvita app that make the psvita a touchpad for the PC. 
Expected behaviour:
+ when Im touching, dragging, tapping the psvita screen, it will broadcast the mouse event to the PC via udp (assume PC has open a server to receive the data).
+ when in the main screen (touchpad mode), the screen go into save battery mode (turn off backlight or dim it as low as possible).
+ click start button turn the touchbar into scroll mode - send the http request or use the same udp to toggle the mode too in the PC server, click again to turn it off.
+ have a setting page when click select (turn back the backlight) to let user input the server ip address and passcode