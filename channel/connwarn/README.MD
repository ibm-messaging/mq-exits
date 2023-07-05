# CONNWARN
A sample security exit for detecting and warning applications which would fail Authentication checks.

## Overview
This sample implements a security exit to provide connection authentication warning mode functionality. 

When called as a security exit on a Server-Connection channel it will examine the `MQCD` and `MQCSP` structures sent by the client and output information about whether credentials have been sent via them. If credentials are set in the MQCSP structure then validate them against the Operating System.

The security exit is designed as a diagnostics or test tool only and so does not alter the structures it is passed and will not cause a connection to fail unless there is a problem with the exit (e.g. unable to log or failed to validate credentials on UNIX). While this security exit currently only supports OS validation, but could be extended to validate againsts a LDAP Server instead.

**Note:** This sample assumes a default installation location of MQ, if you do not have MQ installed in a default location then you will need to update update the `connwarn.c` file to specify the location of the IBM MQ installation.

## Building
The code needs to be built as a **64-bit** threaded, dynamically-loaded module with `ChlExit` as the entrypoint. An example `.sh` script and `.cmd` script are supplied to show how to build on UNIX and Windows platforms. **Note:** On windows you must link with the `Advapi32.lib` module. 
Full instructions on building channel exits can be found in the [MQ documentation](https://www.ibm.com/support/knowledgecenter/SSFKSJ_latest/com.ibm.mq.dev.doc/q028160_.htm). 

Once built the exit must be copied into the MQ exits64 directory located under the MQ Data Directory (Commonly `/var/mqm` for UNIX or `C:\ProgramData\IBM\MQ` for Windows).

## Configuration
This security exit sample can be used with connection authentication disabled on the queue manager as it is not reliant on the connection authentication feature. By disabling connection authentication by setting the `AUTHINFO` object `CHCKCLNT` value to `(NONE)` or blanking the QMGR CONNAUTH attribute your applications will be able to connect without failing the connection authentication step but having the contents of the `MQCD` and `MQCSP` fields tested by this sample.

Security exits are configured via the channel object that you want the security exit to be active on. There are two attributes that relate to security exits. `SCYEXIT` and `SCYDATA`. 
* [SCYEXIT](https://www.ibm.com/support/knowledgecenter/SSFKSJ_latest/com.ibm.mq.ref.con.doc/q082160_.htm) is used to specify the security exit to use and entrypoint within to call. For example: `SCYEXIT('connwarn(ChlExit)')`
* [SCYDATA](https://www.ibm.com/support/knowledgecenter/SSFKSJ_latest/com.ibm.mq.ref.con.doc/q082170_.htm) is used to pass user data to the security exit. For this sample connection authentication warn security exit SCYDATA is used to configure where to place output.

This security exit will attempt to write output to the location indicated in the SCYDATA parameter on the channel or, if that is blank, the default location of `/var/mqm/errors/` on UNIX and `C:\ProgramData\IBM\MQ\errors` on Windows. If you use SCYDATA to change the output location you need to ensure that the mqm user or group has write access to the location otherwise the exit will fail to write an output file and then attempt to write out to the default location instead. If the exit cannot write to the default location then no output will be produced.
The `SCYDATA` field has a maximum size of 32 characters. If the path you want to log to is larger than 32 characters then you can either use symlinks or alter the default location in the `connwarn.c` file.

## Example output
Within either the default directory or the location specified in SCYDATA, log files will be created that match the channel name the security exit is printing for (with special characters replaced). Each entry in the file is outputted in YAML format with the following details:

* Timestamp of the connection in RFC 3339 format. `timestamp`
* The IP of the remote connection. `remote_conname`
* What user the application was running under. `remote_appuser`
* Whether credentials were supplied in the MQCD. `MQCD_set`
* What user was supplied in the MQCD. `MQCD_user`
* Whether credentials were supplied in the MQCSP. `MQCSP_set`
* What user was set in hte MQCSP. `MQCSP_user`
* Whether the credentials in the MQCD and the MQCSP are identical to eachother, see below for more information. `MQCD_MQCSP_identical`
* Whether the MQCSP credentials were valid. I.E. could be used to login to the OS. `MQCSP_valid`
* The version and type of the other end of the channel. Both the 4 character code and a more readable expansion of that code are given.

During operations, if user credentials are supplied via the MQCD but not via the MQCSP then IBM MQ will attempt to copy the credentials from the MQCD into the MQCSP. As such in the case where an application sends credentials only via the MQCD object it can appear that credentials were sent via the MQCSP. To attempt to indicate when this may of happened, this sample will compare the credentials in the MQCSP fields wiht those in the MQCD field and if they match will indicate this.

Below is an example of what an entry in the log output may look like:
```
-
  timestamp: "2020-11-04T11:45:04+0000"
  remote_conname: "127.0.0.1"
  remote_appuser: "mqm"
  remote_version: "09030300"
  remote_product: "MQCC"
  remote_product_decode: "IBM MQ C client"
  MQCD_set: false
  MQCD_user: ""
  MQCSP_set: false
  MQCSP_user: ""
  MQCD_MQCSP_identical: false
  MQCSP_valid: false
```

## Notes
* To validate credentials on UNIX applications must have root priviledges and IBM MQ exits are not executed with root permissions. In order to validate credentials on UNIX, this sample will use the `amqoampx` executable. `amqoampx` is not documented as it is an internal program for use by IBM MQ and not intended to be used by users of IBM MQ. This is assumed to be in the default installation directory and if not present will cause the MQCSP validating function to fail.

* On Windows, this security exit can trigger userids to be locked out if applications connect with an incorrect password multiple times as each time the credential is checked incorrectly it can cause Windows to automatically lock a user depending on system settings. 

* The log file will keep growing unless a external mechanism is setup to rotate the logs.

## Change History
* 1st November 2020     Initial release
* 4th July     2023     Enable AIX. Add recording of remote product version.     
