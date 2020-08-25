#!/bin/sh

# This script makes sure a qmgr has the correct configuration
# and then does a few things to create a logfile. At the end
# the qmgr is shutdown.

qm="OAMLOG"
mod=oamlog_r

make -f Makefile.linux
if [ $? -ne 0 ]
then
  exit 1
fi

. setmqenv -m $qm -k

endmqm -i $qm

grep -q $mod /var/mqm/qmgrs/$qm/qm.ini
if [ $? -ne 0 ]
then
cat << EOF
Insert this stanza in /var/mqm/qmgrs/$qm/qm.ini before
the amqzfu service.
--------------------------
ServiceComponent:
  Service=AuthorizationService
  Name=Auditing.Auth.Service
  Module=/var/mqm/exits64/$mod
  ComponentDataSize=0
---------------------------
EOF
fi

# This logfile needs to match the value of LOGFILE
# in the source code.
logFile=/var/mqm/audit/oamlog.log
d=`dirname $logFile`
mkdir -p $d

> $logFile

chmod 660 $logFile

strmqm $qm

# Run any command to make sure there's something other than CheckAuth calls in there
setmqaut -m $qm -p root -t qmgr +connect

endmqm -i $qm

more $logFile
