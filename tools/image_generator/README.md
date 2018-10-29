Recovery Image Generator
-------------------------

This program uses java.awt.Graphics2D to generate the background text files used
under recovery mode. And thus we don't need to do the manual work by running
emulators with different dpi.

# Usage:
  `java -jar path_to_jar --image_width imageWidth --text_name textName --font_dir fontDirectory
   --resource_dir resourceDirectory --output_file outputFilename`

# Description of the parameters:
1. `imageWidth`: The number of pixels per line; and the text strings will be
   wrapped accordingly.
2. `textName`: The description of the text string, e.g. "recovery_erasing",
   "recovery_installing_security"
3. `fontDirectory`: The directory that contains all the support .ttf | .ttc
   files, e.g. $OUT/system/fonts/
4. `resourceDirectory`: The resource directory that contains all the translated
   strings in xml format, e.g. bootable/recovery/tools/recovery_l10n/res/
5. `outputFilename`: Path to the generated image.
