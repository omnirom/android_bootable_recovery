#!/usr/bin/env python
# -*- coding: utf8 -*-
import codecs,os,gzip,ctypes,ctypes.util,sys
from struct import *
from PIL import Image, ImageDraw, ImageFont

# ====== Python script to convert TrueTypeFonts to TWRP's .dat format ======
# This script was originally made by https://github.com/suky for his chinese version of TWRP
# and then translated to English by feilplane at #twrp of irc.freenode.net.
# However, it was not compatible with vanilla TWRP, so https://github.com/Tasssadar rewrote
# most of it and it now has very little in common with the original script.

class Reference():
    def __init__(self, val):
        self.__value = val

    def get(self):
        return self.__value

    def set(self, val):
        self.__value = val

quiet = Reference(False)

def log(text):
    if not quiet.get():
        sys.stdout.write(text)

def write_data(f, width, height, offsets, data):
    f.write(pack("<I", width))
    f.write(pack("<I", height))
    for off in offsets:
        f.write(pack("<I", off))
    f.write(data)

if __name__ == "__main__":
    fontsize = Reference(20)
    out_fname = Reference("font.dat")
    voffset = Reference(None)
    padding = Reference(0)
    font_fname = Reference(None)
    preview = Reference(None)

    arg_parser = [
        ["-s", "--size=", fontsize, int],
        ["-o", "--output=", out_fname, str],
        ["-p", "--preview=", preview, str],
        [None, "--padding=", padding, int],
        ["-q", "--quiet", quiet, None],
        [None, "--voffset=", voffset, int]
    ]

    argv = sys.argv
    argc = len(argv)
    i = 1
    while i < argc:
        arg = argv[i]
        arg_next = argv[i+1] if i+1 < argc else None

        if arg == "--help" or arg == "-h":
            print ("This script converts TrueTypeFonts to .dat file for TWRP recovery.\n\n"
                "Usage: %s [SWITCHES] [TRUETYPE FILE]\n\n"
                "  -h, --help                   - print help\n"
                "  -o, --output=[FILE]          - output file or '-' for stdout (default: font.dat)\n"
                "  -p, --preview=[FILE]         - generate font preview to png file\n"
                "  --padding=[PIXELS]           - horizontal padding around each character (default: 0)\n"
                "  -q, --quiet                  - Do not print any output\n"
                "  -s, --size=[SIZE IN PIXELS]  - specify font size in points (default: 20)\n"
                "  --voffset=[PIXELS]           - vertical offset (default: font size*0.25)\n\n"
                "Example:\n"
                "  %s -s 40 -o ComicSans_40.dat -p preview.png ComicSans.ttf\n") % (
                    sys.argv[0], sys.argv[0]
                )
            exit(0)

        found = False
        for p in arg_parser:
            if p[0] and arg == p[0] and (arg_next or not p[3]):
                if p[3]:
                    p[2].set(p[3](arg_next))
                else:
                    p[2].set(True)
                i += 1
                found = True
                break
            elif p[1] and arg.startswith(p[1]):
                if p[3]:
                    p[2].set(p[3](arg[len(p[1]):]))
                else:
                    p[2].set(True)
                found = True
                break

        if not found:
            font_fname.set(arg)

        i += 1

    if not voffset.get():
        voffset.set(int(fontsize.get()*0.25))

    if out_fname.get() == "-":
        quiet.set(True)

    log("Loading font %s...\n" % font_fname.get())
    font = ImageFont.truetype(font_fname.get(), fontsize.get(), 0, "utf-32be")
    cwidth = 0
    cheight = font.getsize('A')[1]
    offsets = []
    renders = []
    data = bytes()

    # temp Image and ImageDraw to get access to textsize
    res = Image.new('L', (1, 1), 0)
    res_draw = ImageDraw.Draw(res)

    # Measure each character and render it to separate Image
    log("Rendering characters...\n")
    for i in range(32, 128):
        w, h = res_draw.textsize(chr(i), font)
        w += padding.get()*2
        offsets.append(cwidth)
        cwidth += w
        if h > cheight:
            cheight = h
        ichr = Image.new('L', (w, cheight*2))
        ichr_draw = ImageDraw.Draw(ichr)
        ichr_draw.text((padding.get(), 0), chr(i), 255, font)
        renders.append(ichr)

    # Twice the height to account for under-the-baseline characters
    cheight *= 2

    # Create the result bitmap
    log("Creating result bitmap...\n")
    res = Image.new('L', (cwidth, cheight), 0)
    res_draw = ImageDraw.Draw(res)

    # Paste all characters into result bitmap
    for i in range(len(renders)):
        res.paste(renders[i], (offsets[i], 0))
        # uncomment to draw lines separating each character (for debug)
        #res_draw.rectangle([offsets[i], 0, offsets[i], cheight], outline="blue")

    # crop the blank areas on top and bottom
    (_, start_y, _, end_y) = res.getbbox()
    res = res.crop((0, start_y, cwidth, end_y))
    cheight = (end_y - start_y) + voffset.get()
    new_res = Image.new('L', (cwidth, cheight))
    new_res.paste(res, (0, voffset.get()))
    res = new_res

    # save the preview
    if preview.get():
        log("Saving preview to %s...\n" % preview.get())
        res.save(preview.get())

    # Pack the data.
    # The "data" is a B/W bitmap with all 96 characters next to each other
    # on one line. It is as wide as all the characters combined and as
    # high as the tallest character, plus padding.
    # Each byte contains info about eight pixels, starting from
    # highest to lowest bit:
    # bits:   | 7  6  5  4  3  2  1  0 | 15 14 13 12 11 10 9  8  | ...
    # pixels: | 0  1  2  3  4  5  6  7 | 8  9  10 11 12 13 14 15 | ...
    log("Packing data...\n")
    bit = 0
    bit_itr = 0
    for c in res.tostring():
        # FIXME: How to handle antialiasing?
        # if c != '\x00':
        # In Python3, c is int, in Python2, c is string. Because of reasons.
        try:
            fill = (ord(c) >= 127)
        except TypeError:
            fill = (c >= 127)
        if fill:
            bit |= (1 << (7-bit_itr))
        bit_itr += 1
        if bit_itr >= 8:
            data += pack("<B", bit)
            bit_itr = 0
            bit = 0

    # Write them to the file.
    # Format:
    # 000: width
    # 004: height
    # 008: offsets of each characters (96*uint32)
    # 392: data as described above
    log("Writing to %s...\n" % out_fname.get())
    if out_fname.get() == "-":
        write_data(sys.stdout, cwidth, cheight, offsets, data)
    else:
        with open(out_fname.get(), 'wb') as f:
            write_data(f, cwidth, cheight, offsets, data)

    exit(0)
