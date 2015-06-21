**Team Win Recovery Project (TWRP)**

The goal of this branch is to rebase TWRP onto AOSP while maintaining as much of the original AOSP code as possible. This goal should allow us to apply updates to the AOSP code going forward with little to no extra work.  With this goal in mind, we will carefully consider any changes needed to the AOSP code before allowing them.  In most cases, instead of changing the AOSP code, we'll create our own functions instead.  The only changes that should be made to AOSP code should be those affecting startup of the recovery and some of the make files.

If there are changes that need to be merged from AOSP, we will pull the change directly from AOSP instead of creating a new patch in order to prevent merge conflicts with AOSP.

This branch is under final testing and will be used shortly for public builds, but has not officially been released.

You can find a compiling guide [here](http://forum.xda-developers.com/showthread.php?t=1943625 "Guide").

[More information about the project.](http://www.teamw.in/project/twrp2 "More Information")

If you have code changes to submit those should be pushed to our gerrit instance.  A guide can be found [here](http://teamw.in/twrp2-gerrit "Gerrit Guide").

步骤/steps:
基于omni5.1源码编译/ Based on omni5.1

1.cd omni

2.source build/envsetup.sh

3.lunch

or

lunch omni_yourdevice-eng

4.make -j4 recoveryimage

最终revocery文件会生成在omni/out/target/product/yourdevice/recovery.img


编译完成后，如果想编译另一个版本twrp，则替换omni/bootable下整个recovery
再make clean清除已编译的文件，再次重新编译make -j4 recoveryimage

。
