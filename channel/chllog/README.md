This directory contains a channel (and preconnect) exit that simply logs its invocations.

It can be configured on any of the channel-related exit points: I wrote it because of some questions on the behaviour of
a lesser-used exit. This helped me validate under what conditions the exit would be called.

To build on Linux, use `make -f Makefile.linux`. Then copy `chllog` to _/var/mqm/exits64_

To build on Windows, use `build.bat`. The file may require editing to point to your Visual Studio installation. Then
copy `bin\chllog.dll` to the _exits64_ directory under your MQ installation's data directory.

An example configuration is provided to invoke the exit from a client program, but you can also configure channel exits
at the queue manager end.

Set the `CHLLOG_LOG_FILE` environment variable to point at a log file. For clients, you could also use _stdout_ or
_stderr_. Those streams are not easily accessible from the exits running at the queue manager end of a channel of
course.

To use in a client, you need
* MQCHLLIB, MQCHLTAB to point at a CCDT
* A channel definition that has the exits defined as `chllog(ChlExit)`
* MQCLNTCF to point at a client.ini file referencing the directory holding the exit

The _RUNME.sh_ script handles all of that setup for a Linux system, and runs a simple program to demonstrate the output.

```
make: 'chllog' is up to date.
Sample AMQSPUT0 start
2026/09/28 11:13:19 Opened logfile stdout
2026/09/28 11:13:19 Chl: SYSTEM.DEF.SVRCONN   Exit : SEND [13]
2026/09/28 11:13:19                           Cause: INIT [11]
2026/09/28 11:13:19 Chl: SYSTEM.DEF.SVRCONN   Exit : RCV [14]
2026/09/28 11:13:19                           Cause: INIT [11]
2026/09/28 11:13:19 Chl: SYSTEM.DEF.SVRCONN   Exit : SEC [11]
2026/09/28 11:13:19                           Cause: INIT [11]
2026/09/28 11:13:19 Chl: SYSTEM.DEF.SVRCONN   Exit : SEC [11]
2026/09/28 11:13:19                           Cause: INIT_SEC [16]
2026/09/28 11:13:19 Chl: SYSTEM.DEF.SVRCONN   Exit : SEC [11]
2026/09/28 11:13:19                           Cause: SEC_PARMS [29]
2026/09/28 11:13:19 Chl: SYSTEM.DEF.SVRCONN   Exit : SEND [13]
2026/09/28 11:13:19                           Cause: XMIT [14]
...
```




