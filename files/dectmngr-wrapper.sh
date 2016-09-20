#!/bin/sh

# Unload and re-load Dect firmware stack to force a SW reset

# Insert kernel module if internal Dect
grep -qE "^dect[[:space:]]" /proc/modules && rmmod dect
[ -s /lib/modules/*/extra/dect.ko ] && insmod /lib/modules/*/extra/dect.ko


# Start dectmngr2 and wait for it to exit
dectmngr2 $*
res=$?


# Unload internal dect firmware stack
grep -qE "^dect[[:space:]]" /proc/modules && rmmod dect


# Exit with same status as from Dectmngr2
exit $res

