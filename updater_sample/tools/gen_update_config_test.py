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
Tests gen_update_config.py
"""

import os.path
import unittest
from gen_update_config import GenUpdateConfig


class GenUpdateConfigTest(unittest.TestCase): # pylint: disable=missing-docstring

    def test_ab_install_type_streaming(self):
        """tests if streaming property files' offset and size are generated properly"""
        config, package = self._generate_config()
        property_files = config['ab_streaming_metadata']['property_files']
        self.assertEqual(len(property_files), 5)
        with open(package, 'rb') as pkg_file:
            for prop in property_files:
                filename, offset, size = prop['filename'], prop['offset'], prop['size']
                pkg_file.seek(offset)
                data = pkg_file.read(size).decode('ascii')
                # data in the archive are just uppercase filenames without extension
                expected_data = filename.split('.')[0].upper()
                self.assertEqual(data, expected_data)

    @staticmethod
    def _generate_config():
        """Generates JSON config from ota_002_package.zip."""
        ota_package = os.path.join(os.path.dirname(__file__),
                                   '../tests/res/raw/ota_002_package.zip')
        gen = GenUpdateConfig(ota_package,
                              'file:///foo.bar',
                              GenUpdateConfig.AB_INSTALL_TYPE_STREAMING)
        gen.run()
        return gen.config, ota_package


if __name__ == '__main__':
    unittest.main()
