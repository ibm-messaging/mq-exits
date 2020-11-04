#!/bin/bash
# Example build bash script for building connwarn using gcc.
# (C) IBM Corporation 2020
#

set -e

gcc -m64 -I/opt/mqm/inc -shared -fPIC -o connwarn connwarn.c 
