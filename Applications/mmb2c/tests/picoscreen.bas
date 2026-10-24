' What picanoid and picofrog say to the screen first.  RESOLUTION 640
' is MODE 1 at the one resolution there is, whatever its speed; a ')'
' after a command's last argument is ignored, as the firmware ignores
' what follows an argument; MM.INFO$(SYSTEM I2C) names the clock's bus.
Resolution 640, 315000
Print "res"; MM.HRes; MM.VRes
Pixel 1, 2, RGB(255, 255, 255))
Print "pixel ok"
Print "i2c "; MM.Info$(SYSTEM I2C)
