# Steps to regenerate background text images under res-*dpi/images/

1.  Build the recovery_l10n app:

    cd bootable/recovery && mma -j32

2.  Install the app on the device (or emulator) with the intended dpi.

    *   For example, we can use Nexus 5 to generate the text images under
        res-xxhdpi.
    *   We can set up the maximum width of the final png image in res/layout/main.xml
        Currently, the image width is 1200px for xxxhdpi, 900px for xxhdpi and
        480px for xhdpi/hdpi/mdpi.
    *   When using the emulator, make sure the NDK version matches the current
        repository. Otherwise, the app may not work properly.

    adb install $PATH_TO_APP

3.  Run the app, select the string to translate and press the 'go' button.

4.  After the app goes through the strings for all locales, pull the output png
    file from the device.

    adb root && adb pull /data/data/com.android.recovery_l10n/files/text-out.png

5.  Compress the output file put it under the corresponding directory.

    *   "pngcrush -c 0 ..." converts "text-out.png" into a 1-channel image,
        which is accepted by Recovery. This also compresses the image file by
        ~60%.
    *   zopflipng could further compress the png files by ~10%, more details
        in https://github.com/google/zopfli/blob/master/README.zopflipng
    *   If you're using other png compression tools, make sure the final text
        image works by running graphic tests under the recovery mode.

    pngcrush -c 0 text-out.png $OUTPUT_PNG
