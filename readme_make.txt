What I do for atari flashbacks, given a buildroot generated sdk in place:

First get the environment variables set:
. ./environment.sh

Then issue this make command:
make platform=armv7-neon-hardfloat-cortexa9

There are other specific optimizations that could be tested (fast-math and so on) but the EMU performs well
even without those, so I have not gone there yet.
