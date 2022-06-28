#!/bin/sh

# Simple build and test on Linux
# This script makes sure a qmgr has the correct configuration
# and then does a few things to create a logfile. It assumes you
# already have a qmgr called OAMLOG.
qm="OAMLOG"
mod=oamok

make -f Makefile.Linux
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
Insert this stanza in /var/mqm/qmgrs/$qm/qm.ini AFTER
the amqzfu service.
--------------------------
ServiceComponent:
  Service=AuthorizationService
  Name=Auditing.Auth.Service
  Module=/var/mqm/exits64/$mod
  ComponentDataSize=0
---------------------------
EOF
  exit 1
fi

# This logfile needs to match the value of LOGFILE
# in the source code.
logFile=/var/mqm/audit/oamok.log
d=`dirname $logFile`
mkdir -p $d

rm -f $logFile
touch $logFile
chmod 660 $logFile

# Uncomment the following line for a pretty-print version of the output
#export AMQ_OAMAUDIT_MULTILINE=true
strmqm $qm

# The first line of the input is the user's password
MQSAMP_USER_ID=admin $MQ_INSTALLATION_PATH/samp/bin/amqsput DEV.QUEUE.0 $qm << EOF
passw0rd
Hello
EOF

sleep 3
more $logFile
