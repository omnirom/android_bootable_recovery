#!/usr/bin/env python
#
# Copyright (C) 2018 The Android Open Source Project
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

"""
Parses a input care_map.txt in plain text format; converts it into the proto
buf message; and writes the result to the output file.

"""

import argparse
import logging
import sys

import care_map_pb2


def GenerateCareMapProtoFromLegacyFormat(lines):
  """Constructs a care map proto message from the lines of the input file."""

  # Expected format of the legacy care_map.txt:
  # system
  # system's care_map ranges
  # [vendor]
  # [vendor's care_map ranges]
  # ...
  assert len(lines) % 2 == 0, "line count must be even: {}".format(len(lines))

  care_map_proto = care_map_pb2.CareMap()
  for index in range(0, len(lines), 2):
    info = care_map_proto.partitions.add()
    info.name = lines[index]
    info.ranges = lines[index + 1]

    logging.info("Adding '%s': '%s' to care map", info.name, info.ranges)

  return care_map_proto


def ParseProtoMessage(message):
  """Parses the care_map proto message and returns its text representation.
  Args:
    message: care_map in protobuf message

  Returns:
     A string of the care_map information, similar to the care_map legacy
     format.
  """
  care_map_proto = care_map_pb2.CareMap()
  care_map_proto.MergeFromString(message)

  info_list = []
  for info in care_map_proto.partitions:
    assert info.name, "partition name is required in care_map"
    assert info.ranges, "source range is required in care_map"
    info_list += [info.name, info.ranges]

    # TODO(xunchang) add a flag to output id & fingerprint also.
  return '\n'.join(info_list)


def main(argv):
  parser = argparse.ArgumentParser(
      description=__doc__,
      formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument("input_care_map",
                      help="Path to the legacy care_map file (or path to"
                           " care_map in protobuf format if --parse_proto is"
                           " specified).")
  parser.add_argument("output_file",
                      help="Path to output file to write the result.")
  parser.add_argument("--parse_proto", "-p", action="store_true",
                      help="Parses the input as proto message, and outputs"
                           " the care_map in plain text.")
  parser.add_argument("--verbose", "-v", action="store_true")

  args = parser.parse_args(argv)

  logging_format = '%(filename)s %(levelname)s: %(message)s'
  logging.basicConfig(level=logging.INFO if args.verbose else logging.WARNING,
                      format=logging_format)

  with open(args.input_care_map, 'r') as input_care_map:
    content = input_care_map.read()

  if args.parse_proto:
    result = ParseProtoMessage(content)
  else:
    care_map_proto = GenerateCareMapProtoFromLegacyFormat(
        content.rstrip().splitlines())
    result = care_map_proto.SerializeToString()

  with open(args.output_file, 'w') as output:
    output.write(result)


if __name__ == '__main__':
  main(sys.argv[1:])
