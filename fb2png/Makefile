# NDK
CC := arm-linux-androideabi-gcc
CFLAGS += -g -static -DANDROID
LDFLAGS += -lpng -lz -lm

ALL: fb2png adb_screenshoot

fb2png: main.o fb.o img_process.o fb2png.o
	$(CC) $(CFLAGS) main.o fb.o img_process.o fb2png.o -o fb2png $(LDFLAGS)
	# $(CC) $(CFLAGS) main.o fb.o img_process.o fb2png.o -o fb2png

adb_screenshoot: adb_screenshoot.o fb.o img_process.o
	 $(CC) $(CFLAGS) adb_screenshoot.o fb.o img_process.o -o adb_screenshoot $(LDFLAGS)

clean:
	rm -f *.o
	rm -f fb2png adb_screenshoot
