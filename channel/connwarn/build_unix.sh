#!/bin/bash
# Example build bash script for building connwarn using gcc.
# (C) IBM Corporation 2020
#

set -e

gcc -shared -fPIC -I/opt/mqm/inc -o /var/mqm/exits64/connwarn connwarn.c 
