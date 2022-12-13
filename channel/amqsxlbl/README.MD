# amqsxlbl
A sample channel auto-definition (CHAD) exit for setting the CERTLABL attribute for CLUSSDR channels.

## Overview
This sample implements a CHAD exit to provide a configurable mapping of the `CERTLABL` attribute for both manually- and automatically-defined cluster sender channel objects.

When called as a CHAD exit on a CLUSSDR channel it will compare the channel and cluster name against mapping entries contained within a configuration file, and update the `CERTLABL` field of the channel definition based on the first matching mapping found. If no mapping is found, the `CERTLABL` attribute is left as its default (blank) value and the queue manager's default certificate label will be used for the channel.

A sample configuration file, amqsxlbl.ini is included, with comments describing the structure of the configuration file.

The configuration file must be supplied to the exit using the environment variable `MQXLBL_CONFIG_FILE`, which must be set to the fully-qualified path of the configuration file, and exported in the environment used to start the queue manager. For example:

```export MQXLBL_CONFIG_FILE=/var/mqm/exits64/amqsxlbl.ini```

**Note:** This sample assumes a default installation location of MQ, if you do not have MQ installed in a default location then you will need to update update the `connwarn.c` file to specify the location of the IBM MQ installation.

## Building
The code needs to be built as a **64-bit** threaded, dynamically-loaded module with `ChlExit` as the entrypoint. An example `.sh` script and `.cmd` script are supplied to show how to build on Linux and Windows platforms. 
Full instructions on building channel exits can be found in the [MQ documentation](https://www.ibm.com/docs/en/ibm-mq/9.3?topic=cepmc-compiling-channel-exit-programs-aix-linux-windows-systems). 



## Configuration
Once built the exit may be copied into the MQ exits64 directory located under the MQ Data Directory (Commonly `/var/mqm` for UNIX or `C:\ProgramData\IBM\MQ` for Windows). At this point the queue manager `CHADEXIT` attribute configuration can be updated to reference the exit.

For example, to set this using the runmqsc console: 
```
ALTER QMGR CHADEXIT('amqsxlbl(ChlExit)')
```

Alternatively, the fully-qualified path to the exit can be provided as part of the CHADEXIT configuration string.

Once configured, the exit will be invoked during the starting of any auto-defined or cluster channel by the queue manager. If called for any channel type that is not a CLUSSDR, the exit takes no action. For a CLUSSDR, the channel's name and cluster membership(s) will be evaluated against the rules specified in the configuration file specified by the `MQXLBL_CONFIG_FILE` environment variable. 

Mapping rules are evaluated in the order listed in the file (starting at the line 1), and the first match against the channel or cluster name will be accepted. When a match is accepted, the certificate label value specified in that line of the configuration file is copied into the `CERTLABL` field of the channel definition, and will be used for the TLS handshake intiated during channel start. No validation is performed by the exit on the existence or validity of the `CERTLABL` value, so specifying an invalid or non-existent certificate may cause the channel to fail to start with a TLS error.

The exit will optionally write informational log messages on its activity for debugging and validation purposes, if the `MQXLBL_LOG_FILE` environment variable references a fully-qualified file path to which the exit has write access. The file will be appended to if it already exists. The log file will keep growing unless a external mechanism is set up to rotate the logs.

## Change History
* 13th December 2022     Initial release
