#!/bin/bash
# Example build bash script for building extjwtexit using gcc.
# (C) IBM Corporation 2023
#

set -e

sudo gcc -m64 -shared -fPIC -o /var/mqm/exits64/extjwtexit ./extjwtexit.c -I/opt/mqm/inc -ljson-c -lcurl
