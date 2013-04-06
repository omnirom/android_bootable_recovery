#!/usr/bin/python

import socket
import sys
import struct
import time

# debug
VERBOSE = True

def D(msg):
    if VERBOSE: print(msg)

# "struct fbinfo" is defined in $T/system/core/adb/framebuffer_service.c
def fbinfo_unpack(data):
    keys = ("version",
            "bpp",
            "size",
            "width",
            "height",
            "red_offset",
            "red_length",
            "blue_offset",
            "blue_length",
            "green_offset",
            "green_length",
            "alpha_offset",
            "alpha_length"
        )
    # the data is little-endian
    values = struct.unpack("<IIIIIIIIIIIII",data)

    D("dump struct fbinfo")
    i = 0
    for key in keys:
        D("%14s: %-12d" % (key, values[i]))
        i = i + 1



def save():
    f = open('dump', 'w')
    while True:
        data = s.recv(4096 * 16)
        if data == "":
            break
        f.write(data)
    f.close()


def communicate(cmd=None):
    if cmd != None:
        buf = "%04x%s" % (len(cmd), cmd)
        D("<< " + buf)
        s.send(buf)
    data = s.recv(4096)

    D(">> [%s]" % len(data))
    D(data)

    if data[0:4] == 'FAIL':
        return False
    else:
        return True


target = ''
# use getopt module in future
for arg in sys.argv:
    if arg == '-q':
        VERBOSE = False
    if target != 'any':
        # compatiable with "adb -d", redirect commands to usb
        if arg == '-d':
            target = 'usb'
        # compatiable with "adb -e", redirect commands to emulator
        elif arg == '-e':
            target = 'local'

if target == '': target ='any'

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

D("connecting")
try:
    s.connect(("localhost", 5037))
except socket.error:
    print 'Cannot connect to localhost:5037'
    print socket.error
    sys.exit(0)

D("connected")

if not communicate("host:transport-%s" % target):
    sys.exit(1)
#communicate("host:transport-usb:shell:ls /data")
communicate("framebuffer:")

data = s.recv(52)
fbinfo_unpack(data)

t0 = float(time.time())
save()
t1 = float(time.time())
print t1 - t0

