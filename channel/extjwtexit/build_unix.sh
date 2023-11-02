#!/bin/bash
# Example build bash script for building extjwtexit using gcc.
# (C) IBM Corporation 2023
#

EXIT=extjwtexit
gcc -m64 -shared -fPIC -o $EXIT ./$EXIT.c -I/opt/mqm/inc -ljson-c -lcurl
if [ $? -eq 0 ]
then
  # If the build succeeded then copy the exit into the default exit directory 
  # We might need both a threaded and unthreaded module name, even though the
  # binary is the same.
  sudo cp $EXIT /var/mqm/exits64/$EXIT
  sudo cp $EXIT "/var/mqm/exits64/$EXIT"_r
fi  
