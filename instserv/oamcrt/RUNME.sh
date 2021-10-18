#!/bin/sh

#     Licensed under the Apache License, Version 2.0 (the "License");
#     you may not use this file except in compliance with the License.
#     You may obtain a copy of the License at
#
#              http://www.apache.org/licenses/LICENSE-2.0
#
#     Unless required by applicable law or agreed to in writing, software
#     distributed under the License is distributed on an "AS IS" BASIS,
#     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
#     either express or implied.
#
#     See the License for the specific language governing permissions and
#     limitations under the License.                                        
#
# (C) IBM Corporation 2021
#


# This script makes sure a qmgr has the correct configuration
# and then does a few things to create a logfile. At the end
# the qmgr is shutdown.

qm="OAMLOG"
mod=oamcrt_r

make -f Makefile.linux
if [ $? -ne 0 ]
then
  exit 1
fi

. setmqenv -m $qm -k
export PATH=/opt/mqm/samp/bin:$PATH

endmqm -i $qm >/dev/null 2>&1

grep -q $mod /var/mqm/qmgrs/$qm/qm.ini
if [ $? -ne 0 ]
then
cat << EOF
Insert this stanza in /var/mqm/qmgrs/$qm/qm.ini before
the amqzfu service.
--------------------------
ServiceComponent:
  Service=AuthorizationService
  Name=ObjectCreationAuth
  Module=/var/mqm/exits64/$mod
  ComponentDataSize=0
---------------------------
EOF
fi

# This logfile needs to match the value of LOGFILE
# in the source code.
for logFile in /var/mqm/audit/oamcrt.log
do
  d=`dirname $logFile`
  mkdir -p $d
  > $logFile
  chmod 660 $logFile
done


strmqm $qm >/dev/null 2>&1

cat << EOF | runmqsc $qm >/dev/null 2>&1
DELETE QL(NOTALLOWED.1) IGNSTATE(YES)
EOF

echo
echo "* This should fail"
(cat << EOF | runmqsc $qm
DEF QL(NOTALLOWED.1) REPLACE
EOF
) | grep -v Copyright


# amqsput args are Q, QM, openOptions, closeOptions, remoteQmgr, modelQName.
# The slightly-modified version of the product sample allows -1 (for numeric) and "_"
# (for string) parameters to say that the default should be left alone.
echo
echo "* This should fail"
echo hello | bin/sput SYSTEM.DEFAULT.MODEL.QUEUE $qm -1 -1 _ NOTALLOWED | grep -v AMQSPUT

echo
echo "* This should succeed"
echo hello | bin/sput SYSTEM.DEFAULT.MODEL.QUEUE $qm -1 -1 _ ALLOWED   | grep -v AMQSPUT

endmqm -i $qm >/dev/null 2>&1
