# Introduction

This directory contains an MQ API Exit that simply removes OpenTelemetry context information from received MQ messages.

This is intended to help applications where the Instana MQ Tracing exit is installed, but the application is not
prepared to handle additional properties/RFH2 content that has been added during the OTel tracing steps.

One alternative is to set `PROPCTL(NONE)` on the application's queue, but that will also affect the OTel Tracing Exit,
leading to incomplete traces. Instead, we allow the Tracing Exit to do its work, and then remove the superfluous
properties afterwards.

This exit is not needed for JMS/XMS applications, which internally parse any RFH2 structures into properties. These
applications are not affected by any transformation in the message shape. The exit is relevant for applications using
the C language bindings.

Outbound messages are not changed at all by this exit; inbound messages with only the trace context have that context removed.

## Application assumptions
* If an application is prepared to deal with messsage properties, it is assumed to be ok with getting more properties
  than planned. Similarly, if it's prepared to deal with an RFH2, it is ok with getting more properties in the `<usr>`
  folder than planned. So it's only an issue when the app would previously have not expected any extra data.
* The message buffer given by the MQGETting application is large enough to hold the full message including the RFH2 and
  its properties. That's required for the Instana exit to have been able to process the message in the first place.

## How it works
When a message is received, if there's an RFH2 structure and it contains ONLY the OTel-related properties (traceparent
and the optional tracestate) then the RFH2 is stripped, leaving the original contents.

## Building the exit

Execute the Makefile with `make` (on Linux) or `build` (on Windows)
* You may need to adjust paths in the `Makefile` or `build.bat` scripts
* The Windows build script assumes you've got one of the Visual Studio compilers installed

## Installation and Configuration
The `doit` script copies the binaries to a suitable place in the /var/mqm tree. You will need to do something similar
for Windows, depending on how your machine is configured.

You will probably want to use the exit in an MQ client, as it would otherwise affect all locally-connected C-based
applications. The *mqclient.ini* file should be used to point at the exit. And your application may need to use the
`MQCLNTCF` environment variable to point at the *mqclient.ini* file.

If you do want to use the exit for local bindings applications, the queue manager's *qm.ini* file should be used.

In both ini files, the syntax for defining the exit is the same:

```
ApiExitLocal:
  Sequence=10
  Function=EntryPoint
  Module=mqinootel
  Name=MQNoOTelExit
```

If you do configure this at the queue manager level and also have the Instana exit installed on the queue manager, then
the sequence number associated with this exit should be lower than that associated with the Instana exit. That ensures
that the exits are called at the right time ie after that exit has handled the MQGET operation.

The exit is only effective in application processes, whether using local bindings or client connections. It does not
have any effect inside queue manager processes. This includes the `amqrmppa` processes that handle the SVRCONN
connections. If you do install the exit for local binding applications, in `qm.ini`. then setting the
`AMQ_RFH2_PRESERVE` environment variable to any value will disable it for that application.

## Logging/debug
Set the `APIX_LOGFILE` environment variable to see various bits of debug reported during the application's execution. That
variable can point at either a filename, or be set to *stdout* or *stderr* to print to the console. Problems getting the exit
loaded may be easily diagnosed with this log.

The exit also populates a field used by the MQ service trace to show it has been loaded successfully or not.