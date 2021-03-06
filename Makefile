#!make

CC=avr-gcc
OBJCOPY=avr-objcopy
STRIP=avr-strip
OBJDUMP=avr-objdump
CFLAGS=-std=c99 -g -O -Wall -I /usr/lib/avr/include/avr -I C:\WinAVR-20070525\avr\include\avr

# dead code removal recipie from http://gcc.gnu.org/ml/gcc-help/2003-08/msg00128.html
DEADCODESTRIP := -Wl,-static -fvtable-gc -fdata-sections -ffunction-sections -Wl,--gc-sections -Wl,-s

TARGETS=ds1990_attiny13.hex ds1990_attiny45.hex ds1990_atmega168.hex
TARGETS+=ds2413_attiny13.hex ds2413_attiny45.hex ds2413_atmega168.hex
TARGETS+=ds2450_atmega168.hex
TARGETS+=ds2413ex_attiny45.hex
TARGETS+=ds2480_atmega168.hex
TARGETS+=boot_attiny45.hex
# TODO: ds2405? ds2406? ds2408 ds2409? ds2423? ds2450 ds2890?

default: $(TARGETS) $(TARGETS:.hex=.asm)

%_attiny13: %.c ows.c ows.h
	$(CC) ${CFLAGS} -mmcu=attiny13 -o $@ $< ows.c
	avr-size $@

%_attiny45: %.c ows.c ows.h
	$(CC) ${CFLAGS} -mmcu=attiny45 -o $@ $< ows.c
	avr-size $@

%_atmega168: %.c ows.c ows.h
	$(CC) ${CFLAGS} -mmcu=atmega168 -o $@ $< ows.c
	avr-size $@

ds2450_atmega168: ds2450.c ows.c ows.h ow_crc16.c ow_crc16.h
	$(CC) ${CFLAGS} -DWITH_CRC16 -mmcu=atmega168 -o $@ $< ows.c ow_crc16.c

ds2413ex_attiny45: ds2413ex.c ows.c ows.h debounce.c debounce.h
#	$(CC) ${CFLAGS} -mmcu=attiny45 -o $@ $< ows.c debounce.c ows_spm.c ow_crc16.c
	$(CC) ${CFLAGS} -falign-functions=32 -mmcu=attiny45 -Wl,-Map,$@.map,--cref -o $@ -D OWS_CONDSEARCH_ENABLE -D OWS_INTERRUPTS_ENABLE -D OWS_WRITE_ROM_ENABLE -D OWS_SPM_ENABLE $< ows.c debounce.c  ows_spm.c ow_crc16.c
	avr-size ds2413ex_attiny45

boot_attiny45: boot.c ows.h ows.c ows_spm.h ows_spm.c ow_crc16.h ow_crc16.c
	$(CC) ${CFLAGS} $(DEADCODESTRIP) -mmcu=attiny45 -o $@  -D OWS_SPM_ENABLE $< ows.c ows_spm.c ow_crc16.c
	avr-size boot_attiny45

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


