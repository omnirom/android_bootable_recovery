# Copyright (C) 2014 The Android Open Source Project
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

"""Script to take a set of frames (PNG files) for a recovery animation
and turn it into a single output image which contains the input frames
interlaced by row.  Run with the names of all the input frames on the
command line, in order, followed by the name of the output file."""

import sys
try:
  import Image
  import PngImagePlugin
except ImportError:
  print "This script requires the Python Imaging Library to be installed."
  sys.exit(1)

frames = [Image.open(fn).convert("RGB") for fn in sys.argv[1:-1]]
assert len(frames) > 0, "Must have at least one input frame."
sizes = set()
for fr in frames:
  sizes.add(fr.size)

assert len(sizes) == 1, "All input images must have the same size."
w, h = sizes.pop()
N = len(frames)

out = Image.new("RGB", (w, h*N))
for j in range(h):
  for i in range(w):
    for fn, f in enumerate(frames):
      out.putpixel((i, j*N+fn), f.getpixel((i, j)))

# When loading this image, the graphics library expects to find a text
# chunk that specifies how many frames this animation represents.  If
# you post-process the output of this script with some kind of
# optimizer tool (eg pngcrush or zopflipng) make sure that your
# optimizer preserves this text chunk.

meta = PngImagePlugin.PngInfo()
meta.add_text("Frames", str(N))

out.save(sys.argv[-1], pnginfo=meta)
