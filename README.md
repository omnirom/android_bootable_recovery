**Team Win Recovery Project (TWRP)**

The goal of this branch is to rebase TWRP onto AOSP while maintaining as much of the original AOSP code as possible. This goal should allow us to apply updates to the AOSP code going forward with little to no extra work.  With this goal in mind, we will carefully consider any changes needed to the AOSP code before allowing them.  In most cases, instead of changing the AOSP code, we'll create our own functions instead.  The only changes that should be made to AOSP code should be those affecting startup of the recovery and some of the make files.

This branch is currently a work in progress, however, most features have been implemented and it is now ready for testing.

You can find a compiling guide [here](http://rootzwiki.com/topic/23903-how-to-compile-twrp-from-source/ "Guide").

[More information about the project.](http://www.teamw.in/project/twrp2 "More Information")

If you have code changes to submit those should be pushed to our gerrit instance.  A guide can be found [here](http://teamw.in/twrp2-gerrit "Gerrit Guide").
