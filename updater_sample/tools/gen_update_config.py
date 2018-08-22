#!/usr/bin/env python3
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
Given a OTA package file, produces update config JSON file.

Example:
      $ PYTHONPATH=$ANDROID_BUILD_TOP/build/make/tools/releasetools:$PYTHONPATH \\
            bootable/recovery/updater_sample/tools/gen_update_config.py \\
                --ab_install_type=STREAMING \\
                ota-build-001.zip  \\
                my-config-001.json \\
                http://foo.bar/ota-builds/ota-build-001.zip
"""

import argparse
import json
import os.path
import sys
import zipfile

import ota_from_target_files  # pylint: disable=import-error


class GenUpdateConfig(object):
    """
    A class that generates update configuration file from an OTA package.

    Currently supports only A/B (seamless) OTA packages.
    TODO: add non-A/B packages support.
    """

    AB_INSTALL_TYPE_STREAMING = 'STREAMING'
    AB_INSTALL_TYPE_NON_STREAMING = 'NON_STREAMING'

    def __init__(self,
                 package,
                 url,
                 ab_install_type,
                 ab_force_switch_slot,
                 ab_verify_payload_metadata):
        self.package = package
        self.url = url
        self.ab_install_type = ab_install_type
        self.ab_force_switch_slot = ab_force_switch_slot
        self.ab_verify_payload_metadata = ab_verify_payload_metadata
        self.streaming_required = (
            # payload.bin and payload_properties.txt must exist.
            'payload.bin',
            'payload_properties.txt',
        )
        self.streaming_optional = (
            # care_map.txt is available only if dm-verity is enabled.
            'care_map.txt',
            # compatibility.zip is available only if target supports Treble.
            'compatibility.zip',
        )
        self._config = None

    @property
    def config(self):
        """Returns generated config object."""
        return self._config

    def run(self):
        """Generates config."""
        self._config = {
            '__': '*** Generated using tools/gen_update_config.py ***',
            'name': self.ab_install_type[0] + ' ' + os.path.basename(self.package)[:-4],
            'url': self.url,
            'ab_config': self._gen_ab_config(),
            'ab_install_type': self.ab_install_type,
        }

    def _gen_ab_config(self):
        """Builds config required for A/B update."""
        with zipfile.ZipFile(self.package, 'r') as package_zip:
            config = {
                'property_files': self._get_property_files(package_zip),
                'verify_payload_metadata': self.ab_verify_payload_metadata,
                'force_switch_slot': self.ab_force_switch_slot,
            }

        return config

    @staticmethod
    def _get_property_files(package_zip):
        """Constructs the property-files list for A/B streaming metadata."""

        ab_ota = ota_from_target_files.AbOtaPropertyFiles()
        property_str = ab_ota.GetPropertyFilesString(package_zip, False)
        property_files = []
        for file in property_str.split(','):
            filename, offset, size = file.split(':')
            inner_file = {
                'filename': filename,
                'offset': int(offset),
                'size': int(size)
            }
            property_files.append(inner_file)

        return property_files

    def write(self, out):
        """Writes config to the output file."""
        with open(out, 'w') as out_file:
            json.dump(self.config, out_file, indent=4, separators=(',', ': '), sort_keys=True)


def main():  # pylint: disable=missing-docstring
    ab_install_type_choices = [
        GenUpdateConfig.AB_INSTALL_TYPE_STREAMING,
        GenUpdateConfig.AB_INSTALL_TYPE_NON_STREAMING]
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--ab_install_type',
                        type=str,
                        default=GenUpdateConfig.AB_INSTALL_TYPE_NON_STREAMING,
                        choices=ab_install_type_choices,
                        help='A/B update installation type')
    parser.add_argument('--ab_force_switch_slot',
                        default=False,
                        action='store_true',
                        help='if set device will boot to a new slot, otherwise user '
                              'manually switches slot on the screen')
    parser.add_argument('--ab_verify_payload_metadata',
                        default=False,
                        action='store_true',
                        help='if set the app will verify the update payload metadata using '
                             'update_engine before downloading the whole package.')
    parser.add_argument('package',
                        type=str,
                        help='OTA package zip file')
    parser.add_argument('out',
                        type=str,
                        help='Update configuration JSON file')
    parser.add_argument('url',
                        type=str,
                        help='OTA package download url')
    args = parser.parse_args()

    if not args.out.endswith('.json'):
        print('out must be a json file')
        sys.exit(1)

    gen = GenUpdateConfig(
        package=args.package,
        url=args.url,
        ab_install_type=args.ab_install_type,
        ab_force_switch_slot=args.ab_force_switch_slot,
        ab_verify_payload_metadata=args.ab_verify_payload_metadata)
    gen.run()
    gen.write(args.out)
    print('Config is written to ' + args.out)


if __name__ == '__main__':
    main()
