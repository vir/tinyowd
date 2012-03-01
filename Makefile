#!make

CC=avr-gcc
OBJCOPY=avr-objcopy
STRIP=avr-strip
OBJDUMP=avr-objdump
CFLAGS=-std=c99 -g -O -Wall -I /usr/lib/avr/include/avr -I C:\WinAVR-20070525\avr\include\avr -O

TARGETS=ds1990_attiny13.hex ds1990_attiny45.hex ds1990_atmega168.hex
TARGETS+=ds2413_attiny13.hex ds2413_attiny45.hex ds2413_atmega168.hex
TARGETS+=ds2450_atmega168.hex
# TODO: ds2405? ds2406? ds2408 ds2409? ds2423? ds2450 ds2890?

default: $(TARGETS) $(TARGETS:.hex=.asm)

%_attiny13: %.c ows.c ows.h
	$(CC) ${CFLAGS} -mmcu=attiny13 -o $@ $< ows.c

%_attiny45: %.c ows.c ows.h
	$(CC) ${CFLAGS} -mmcu=attiny45 -o $@ $< ows.c

%_atmega168: %.c ows.c ows.h
	$(CC) ${CFLAGS} -mmcu=atmega168 -o $@ $< ows.c

PROGRAMS=$(TARGETS:.hex=)
ASSEMBLY=$(TARGETS:.hex=.asm)

clean:
	rm -f $(SRECS) $(PROGRAMS) $(ASSEMBLY) $(TARGETS)

%.asm: %
	$(OBJDUMP) -S -d $^ > $@

%-stripped: %
	$(STRIP) $^ -o $@

%.srec: %-stripped
	$(OBJCOPY) -O srec $^ $@

%.hex: %-stripped
	$(OBJCOPY) -O ihex $^ $@


