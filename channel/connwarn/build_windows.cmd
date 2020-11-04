REM Example build bash script for building connwarn using cl. 
REM (C) IBM Corporation 2020   
REM Must be ran with the x64 version of cl on the path.

cl /LD connwarn.c Advapi32.lib
