rem avrdude -c stk200 -p t45 -v -e -U flash:w:test1ws.hex
rem avrdude -c avrisp -P com5 -p t45 -b19200 -v -v -v -v -e -U flash:w:test1ws.hex

rem this works!!
rem C:\arduino-1.0\hardware\tools\avr\bin\avrdude -c avrisp -P com5 -p t45 -b19200 -v -v -v -v -e -U flash:w:blink.hex
rem avrdude -c avrisp -P com5 -p t45 -b19200 -v -v -v -v -e -U flash:w:blink.hex

rem C:\arduino-1.0\hardware\tools\avr\bin\avrdude -c avrisp -P com5 -p t45 -b19200 -v -v -v -v -e -U flash:w:test1ws.hex
rem C:\arduino-1.0\hardware\tools\avr\bin\avrdude -c stk500v1 -P com8 -p m168 -b57600 -v -v -v -v -e -U flash:w:ds1990.hex

echo ~~~ !!!!! PLEASE RESET THE BOARD NOW !!!!! ~~~
rem C:\arduino-1.0\hardware\tools\avr\bin\avrdude -c stk500v1 -P com8 -p m324 -F -b57600 -v -v -e -U flash:w:ds1990.hex
C:\arduino-1.0\hardware\tools\avr\bin\avrdude -c stk500v1 -P com8 -p m324 -F -b57600 -v -v -e -U flash:w:ds2413_atmega168.hex
