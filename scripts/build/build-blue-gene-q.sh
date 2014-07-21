#!/bin/bash

# http://www-01.ibm.com/support/docview.wss?uid=swg27022103&aid=1

# -O5 to enable -ipa
# -s to strip symbols
make clean
make -j 8 CFLAGS="-I. -g -O3 -qmaxmem=-1 -qarch=qp -qtune=qp -DBSAL_DEBUGGER_ENABLE_ASSERT" applications/argonnite
