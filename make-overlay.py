# Copyright (C) 2011 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Script to take a set of frames (PNG files) for a recovery
"installing" icon animation and turn it into a base image plus a set
of overlays, as needed by the recovery UI code.  Run with the names of
all the input frames on the command line, in order."""

import sys
try:
  import Image
except ImportError:
  print "This script requires the Python Imaging Library to be installed."
  sys.exit(1)

# Find the smallest box that contains all the pixels which change
# between images.

print "reading", sys.argv[1]
base = Image.open(sys.argv[1])

minmini = base.size[0]-1
maxmaxi = 0
minminj = base.size[1]-1
maxmaxj = 0

for top_name in sys.argv[2:]:
  print "reading", top_name
  top = Image.open(top_name)

  assert base.size == top.size

  mini = base.size[0]-1
  maxi = 0
  minj = base.size[1]-1
  maxj = 0

  h, w = base.size
  for j in range(w):
    for i in range(h):
      b = base.getpixel((i,j))
      t = top.getpixel((i,j))
      if b != t:
        if i < mini: mini = i
        if i > maxi: maxi = i
        if j < minj: minj = j
        if j > maxj: maxj = j

  minmini = min(minmini, mini)
  maxmaxi = max(maxmaxi, maxi)
  minminj = min(minminj, minj)
  maxmaxj = max(maxmaxj, maxj)

w = maxmaxi - minmini + 1
h = maxmaxj - minminj + 1

# Now write out an image containing just that box, for each frame.

for num, top_name in enumerate(sys.argv[1:]):
  top = Image.open(top_name)

  out = Image.new("RGB", (w, h))
  for i in range(w):
    for j in range(h):
      t = top.getpixel((i+minmini, j+minminj))
      out.putpixel((i, j), t)

  fn = "icon_installing_overlay%02d.png" % (num+1,)
  out.save(fn)
  print "saved", fn

# Write out the base icon, which is the first frame with that box
# blacked out (just to make the file smaller, since it's always
# displayed with one of the overlays on top of it).

for i in range(w):
  for j in range(h):
    base.putpixel((i+minmini, j+minminj), (0, 0, 0))
fn = "icon_installing.png"
base.save(fn)
print "saved", fn

# The device_ui_init() function needs to tell the recovery UI the
# position of the overlay box.

print
print "add this to your device_ui_init() function:"
print "-" * 40
print "  ui_parameters->install_overlay_offset_x = %d;" % (minmini,)
print "  ui_parameters->install_overlay_offset_y = %d;" % (minminj,)
print "-" * 40
