#!/bin/bash

curdir=`pwd`

make -f Makefile.linux
if [ $? -ne 0 ]
then
  exit 1
fi

cp chllog /var/mqm/exits64/chllog
cp chllog /var/mqm/exits64/chllog_r

export MQCLNTCF=$curdir/mqclient.ini

export MQCHLLIB=$curdir
export MQCHLTAB=ccdt.json

# Force client mode
export MQ_CONNECT_TYPE=CLIENT

# Where to send the output
export CHLLOG_LOG_FILE=stdout

# To to run a different MQGET program if it exists
get=`which get0`
if [ $? -ne 0 ]
then
  # If not, then run the standard sample
  get=/opt/mqm/samp/bin/amqsget
fi

echo hello | /opt/mqm/samp/bin/amqsput X QM1
$get X QM1
