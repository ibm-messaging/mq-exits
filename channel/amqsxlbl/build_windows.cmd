REM Example build bash script for building amqsxlbl using cl. 
REM (C) IBM Corporation 2022
REM Must be run with the x64 version of cl on the path.

cl /LD amqsxlbl.c "%MQ_INSTALLATION_PATH%\Tools\Lib64\mqm.lib"