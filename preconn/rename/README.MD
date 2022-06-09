# CONNWARN
A sample client preconnect exit to override the supplied QM Name before issuing MQCONN

## Overview
This sample implements a preconnect exit which overrides the QMName supplied to/by the application with one specified in the mqclient.ini (or '*')

This could be useful in several situations, for example if you wish to move an old application which hard-codes the QMName to use a CCDT - without making code changes or rebuilding the application.

## Building
The code and configuration examples assume the module will be be built as a **64-bit** threaded, dynamically-loaded module with `RenamePreconnectExit` as the entrypoint. Example compile commands for common development environments:
* Linux
** gcc -m64 -I/opt/mqm/inc -shared -fPIC -o rename rename.c 
* OSX
** clang rename.c -shared -I $MQ_INSTALLATION_DIR/inc -o rename
* Windows
** cl /LD rename.c

Full instructions on building preconnect exits can be found in the [MQ documentation](https://www.ibm.com/docs/en/ibm-mq/9.2?topic=ueaemis-referencing-connection-definitions-using-pre-connect-exit-from-repository). 

Once built the exit must be copied into the MQ exits64 directory located under the MQ Data Directory (Commonly `/var/mqm` for UNIX or `C:\ProgramData\IBM\MQ` for Windows). Alternatively, the location can be specified in the ini file by modifying and uncommenting the relevant lines.

## Configuration

The client preconnect exit is configured using the example mqclient.ini file provided.  See MQ documentation for further details on mqclient.ini - by default, the new configuration will be picked up if this file is present in the working directory for your client application.

## Example usage


## Notes

## Change History
* June 2022     Initial release
