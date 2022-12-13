#!/bin/bash
# Example build bash script for building amqsxlbl using gcc.
# (C) IBM Corporation 2022
#

set -e

gcc -m64 -I/opt/mqm/inc -shared -fPIC -o amqsxlbl amqsxlbl.c 
