# CONNWARN
A sample security exit for detecting and warning applications which would fail Authentication checks.

## Overview
This sample implements a secuirty exit to provide connection authentication warning mode functionality. 

When called as a security exit on a Server-Connection channel it will examine the `MQCD` and `MQCSP` structures sent by the client and output information about whether credentials have been sent via them. If credentials are set in the MQCSP structure then it will valid them against the Operating System.

The security exit is designed as a diagnostics or test tool only and so does not alter the structures it is passed and will not cause a connection to fail.

## Building
The code needs to be built as a **64-bit** threaded, dynamically-loaded module with `ChlExit` as the entrypoint. A Makefile is included here for Linux; full instructions on building channel exits can be found in the [MQ documentation](https://www.ibm.com/support/knowledgecenter/SSFKSJ_9.2.0/com.ibm.mq.dev.doc/q028160_.htm). An example .sh script and .cmd script are supplied to show how to build on UNIX and Windows platforms. **Note:** On windows you must link with the `Advapi32.lib` module.

Once build the exit must be copied into the MQ exits64 directory located under the MQ Data Directory (Commonly `/var/mqm` for UNIX or `C:\ProgramData\IBM\MQ` for Windows).

## Configuration
Security exits are configured via the channel object that you want the security exit to be active on. There are two attributes that relate to security exits. `SCYEXIT` and `SCYDATA`. 
* [SCYEXIT](https://www.ibm.com/support/knowledgecenter/SSFKSJ_9.2.0/com.ibm.mq.ref.con.doc/q082160_.htm) is used to specify the security exit to use and entrypoint within to call. For example: `SCYEXIT('connwarn(ChlExit)')`
* [SCYDATA](https://www.ibm.com/support/knowledgecenter/SSFKSJ_9.2.0/com.ibm.mq.ref.con.doc/q082170_.htm) is used to pass user data to the security exit. For this sample connection authentication warn security exit SCYDATA is used to configure where to place output.

This security exit will attempt to write output to the location indicated in the SCYDATA parameter on the channel or, if that is blank, the default location of `/var/mqm/errors/` on UNIX and `C:\ProgramData\IBM\MQ\errors` on Windows. If you use SCYDATA to change the output location you need to ensure that the mqm user or group has write access to the location otherwise the exit will fail to write an output file and then attempt to write out to the default location instead. If the exit cannot write to the default location then no output will be produced.

## Example output
Within either the default directory or the location specified in SCYDATA, log files will be created that match the channel name the security exit is printing for (with special characters replaced). Each entry in the file details a connection with the following details:

* Timestamp of the connection in RFC 3339 format
* The IP of the remote connection and what user the application was running under.
* Whether credentials were supplied in the MQCD, and if they were what username.
* Whether credentials were supplied in the MQCSP, and if they were what username.
* Whether the credentials in the MQCD and the MQCSP are identical to eachother (see notes)
* Whether the MQCSP credentials were valid. I.E. could be used to login to the OS.

For example:
```
2020-10-27T15:10:32+0000
Connection from 127.0.0.1 running as mqm
           MQCD Credentials Supplied: N (N/A)
          MQCSP Credentials Supplied: Y (bob)
MQCD and MQCSP Credentials Identical: N/A
             MQCSP Credentials Valid: Y
---------------------------------------
```

## Notes:
* The SCYDATA field has a maximum size of 32 characters. If the path you want to output to is larger than 32 characters then either use symlinks or alter the default location in the `connwarn.c` file.

* On UNIX, this sample will use the `amqoampx` executable to validate credentials. This is because validating credentials is only available to root users which the exit is unlikely to run as. `amqoampx` is not documented as it is an internal program for use in IBM MQ and not intended to be used by users of MQ.

* During operations, if user credentials are supplied via the MQCD but not via the MQCSP then IBM MQ will attempt to copy the credentials from the MQCD into the MQCSP. As such th

* This security exit currently only supports OS validation, but could be extended to validate againsts a LDAP Server instead.

* This security exit sample is best used in a testing environment prior to turning on Connection Authentication to ensure that applications connecting are providing valid credentials. It is not a replacement for connection authentication.

* This security exit sample can be used with connection authentication disabled on the queue manager as it is not reliant on the connection authentication feature. 

* On Windows, this security exit can trigger userids to be locked out if applications connect with an incorrect password multiple times as each time the credential is checked incorrectly it can cause Windows to automatically lock a user depending on system settings. 

## Change History
* 1st November 2020     Initial release
