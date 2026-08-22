OPTION EXPLICIT
OPTION DEFAULT FLOAT
const i2caddr=&b1110111
const MS7=7 'set default wait period
'
dim i2cin$ length 32 'the calibration block is 22 bytes
dim UT%,UP%
dim ac1%,ac2%,ac3,ac4%,ac5%,ac6%,b1%,b2%,mb%,mc%,md% 'bmp180 parameters
dim x1%,x2%,b5%,b6%,x3,b3%,b4%,b7%,OSS%
dim temperature%,pressure%
DIM pressureinHpa
dim OSSdata%(4)
dim OSSscale%(4)


' Is it actually there?  Nothing below raises, so without this a
' missing sensor reads as zeroes and the arithmetic prints nonsense.
I2C CHECK i2caddr
If MM.I2C <> 0 Then
  Print "No BMP180 answering at &h" + Hex$(i2caddr) + " on the QWIIC bus"
  Print "(MM.I2C ="; MM.I2C; ") - 1 means nothing answered, 2 means it"
  Print "started and stopped."
  End
EndIf
Print "BMP180 found at &h" + Hex$(i2caddr)

init:
  OSS%=1 'set oversampling ratio
  OSSdata%(0)=&H34 'commands to sample pressure with different oversampling
  OSSdata%(1)=&H74
  OSSdata%(2)=&Hb4
  OSSdata%(3)=&HF4
  OSSscale%(0)=1 'scale factors for calcs when oversampled
  OSSscale%(1)=2
  OSSscale%(2)=4
  OSSscale%(3)=8
'
  I2C WRITE i2caddr,1,1,&HAA 'send read calibration data command
  I2C READ i2caddr,0,22,i2cin$ 'read in calibration data
  ac1%=str2bin(int16,mid$(i2cin$,1,2),big)
  ac2%=str2bin(int16,mid$(i2cin$,3,2),big)
  ac3=str2bin(int16,mid$(i2cin$,5,2),big)
  ac4%=str2bin(uint16,mid$(i2cin$,7,2),big)
  ac5%=str2bin(uint16,mid$(i2cin$,9,2),big)
  ac6%=str2bin(uint16,mid$(i2cin$,11,2),big)
  b1%=str2bin(int16,mid$(i2cin$,13,2),big)
  b2%=str2bin(int16,mid$(i2cin$,15,2),big)
  mb%=str2bin(int16,mid$(i2cin$,17,2),big)
  mc%=str2bin(int16,mid$(i2cin$,19,2),big)
  md%=str2bin(int16,right$(i2cin$,2),big)
'
dim integer nreads%
'
' one reading now, then serve for two minutes, refreshing every 2 s
readsensor
WEB TCP SERVER PORT 8080
WEB TCP INTERRUPT handler
print "serving on 8080, ip "; MM.Info(IP ADDRESS)
timer = 0
do while timer < 120000
  pause 2000
  readsensor
  nreads% = nreads% + 1
loop
print "done after "; nreads%; " refreshes"
end
'
sub handler
  local integer a, b(512)
  for a = 1 to MM.Info(MAX CONNECTIONS)
    WEB TCP READ a, b()
    if LLen(b()) > 0 then WEB TRANSMIT PAGE a, "temp.html"
  next a
end sub
'
sub readsensor
  I2C WRITE i2caddr,0,2,&HF4,&H2E
  pause MS7
  I2C WRITE i2caddr,1,1,&HF6
  I2C READ i2caddr,0,2,i2cin$
  UT%=str2bin(uint16,i2cin$,big)
  I2C WRITE i2caddr,0,2,&HF4,ossdata%(oss%)
  pause (oss%+1)*ms7
  I2C WRITE i2caddr,1,1,&HF6
  I2C READ i2caddr,0,3,i2cin$
  UP%=str2bin(uint32,chr$(0)+i2cin$,big)
  UP%=UP%>>(8-oss%)
  calc_temp()
  calc_pressure()
  pressureinHpa=pressure%/100
end sub
'
sub calc_temp
  x1%=(UT%-ac6%)*ac5%\powerof2(15)
  x2%=mc%*powerof2(11)/(x1%+md%) 'a floating divide, to match the datasheet
  b5%=x1%+x2%
  temperature%=(b5%+8)\powerof2(4)
end sub
'
' calc_pressure: the pressure from the raw reading, the calibration and
' the temperature
'
sub calc_pressure
   b6%=b5%-4000
   x1%=(b2%*(b6%*b6%/powerof2(12)))\powerof2(11)
   x2%=ac2%*b6%\powerof2(11)
   x3=x1%+x2%
   b3%=(((ac1%*4+x3)*ossscale%(oss%))+2)\4
   x1%=AC3*b6%\POWEROF2(13)
   x2%=(b1%*(b6%*b6%/POWEROF2(12)))\POWEROF2(16)
   x3=((x1%+x2%)+2)\4
   b4%=ac4%*(abs(x3+32768))\powerof2(15)
   b7%=abs(UP%-b3%)*(50000\ossscale%(oss%))
   pressure%=(b7%*2)\b4%
   x1%=(pressure%\powerof2(8))*(pressure%\powerof2(8))
   x1%=(x1%*3038)\powerof2(16)
   x2%=(-7357*pressure%)\powerof2(16)
   pressure%=pressure%+(x1%+x2%+3791)\powerof2(4)
end sub
'
Function powerof2(i as integer) as integer
  powerof2=(1<<i)
End Function
