#!/bin/sh

# Unload and re-load Dect firmware stack to force a SW reset
grep -qE "^dect[[:space:]]" /proc/modules && rmmod dect
[ -s /lib/modules/*/extra/dect.ko ] && insmod /lib/modules/*/extra/dect.ko

dectmngr2 $*
res=$?

# Unload dect firmware stack
grep -qE "^dect[[:space:]]" /proc/modules && rmmod dect

# Exit with same status as from Dectmngr2
exit $res

