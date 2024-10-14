# Introduction

This directory contains an MQ API Exit that propagates OpenTelemetry context information from instrumented C/C++
applications via MQ messages. It can work in conjunction with, but does not require, the Instana MQ Tracing exit. The
exit has been written and tested on Linux platforms only. It MIGHT work on AIX with small tweaks to the Makefile (though
getting the OTel SDK libraries might be more challenging); it would require more changes to build and run on Windows.

The logic follows the same pattern as the OTel processing in the [Go](https://github.com/ibm-messaging/mq-golang) and
[NodeJS](https://github.com/ibm-messaging-mq-mqi-nodejs) MQ bindings. Outbound messages have properties added that can
be picked up by receiving application; inbound messages with context have that context linked into any existing OTel
trace/spans. Even though the Go and NodeJS libraries call the underlying C MQI library (and hence this exit if it is
configured), there is no "double accounting"; the wrappers' OTel work takes precedence if the apps are instrumented.

The OTel libraries are 64-bit only. This API exit is coded so that it only does work in the 64-bit mode, but it can be
loaded (with no real function) in 32-bit applications.

## OpenTelemetry library dependencies
The exits depend on having the opentelemetry cpp libraries already available. Either archive or shared libraries can be
used depending on your preference. But using shared libraries is more likely to need additional configuration such as
`LD_LIBRARY_PATH` to be set so that they can be found and loaded at runtime.

While you might be able to find existing pre-built libraries, it's more likely that you will have to build them
yourself. See [Getting Started material here](https://opentelemetry.io/docs/languages/cpp/getting-started/) for
information on creating these libraries. For this exit, you do not need any of the exporters to be built, which means
that many of the dependencies (eg grpc) are not necessary.

However you do need to build the OTel libraries with the ABI version 2 option. This is done by setting
`-DWITH_ABI_VERSION_2=ON -DWITH_ABI_VERSION_1=OFF` in the *cmake* configuration. You also need
`-DCMAKE_POSITION_INDEPENDENT_CODE=ON`.

This is where I might like to have a rant about how inappropriate C++ is for library packages ... But I'll mostly resist
that temptation.

## Building the exit

Execute the Makefile with `make`! The output from that step contains two things:
* An API exit written in C that loads the main tracing exit. Various MQI calls are then proxied to the tracing exit.
* A tracing exit written in C++ using the OTel C++ tracing libraries. Note that this is really mostly C code,
  as it needs to work with the C MQI. It only gets into C++ mode when essential.

The Makefile might require editing to point at directories where your OTel libraries and include files live.

The base exit includes a 32-bit version that does no real work so that 32-bit applications using the queue manager (if
you configure it at that point) do not actually fail to run.

More likely, you would run the exit in an MQ C client, with the *mqclient.ini* file pointing at the exit.

## Installation and Configuration
The `doit` script copies the binaries to a suitable place in the /var/mqm tree.

If you want to use the exit in an MQ client, then the *mqclient.ini* file should be used to point at the exit. And your
application may need to use the `MQCLNTCF` environment variable to point at the *mqclient.ini* file.If you want to use
the exit for local bindings applications, the queue manager's *qm.ini* file should be used.

In both ini files, the syntax for defining the exit is the same:

```
ApiExitLocal:
  Sequence=10
  Function=EntryPoint
  Module=mqiotel
  Name=MQOTelExit
```

If you configure this at the queue manager level and also have the Instana exit installed on the queue manager, then the
sequence number associated with this exit should be lower than that associated with the Instana exit. That ensures that
the exits are called at the right time.

The exit is only effective in application processes, whether using local bindings or client connections. It does not
have any effect inside queue manager processes. This includes the `amqrmppa` processes that handle the SVRCONN
connections.

## Logging/debug
Set the `APIX_LOGFILE` environment variable to see various bits of debug reported during the application's execution. That
variable can point at either a filename, or be set to *stdout* or *stderr* to print to the console. Problems getting the exit
loaded may be easily diagnosed with this log.

The exit also populates a field used by the MQ service trace to show it has been loaded successfully or not.

## Instrumented applications
Instrumenting your C/C++ applications to use OTel tracing is beyond the scope of this document. The Getting Started page
referenced earlier has useful information.

One requirement is that the application must also be using the ABI V2 OTel libraries. Mixing both V1 and V2 libraries in
the same application process seems to cause confusion. And of course, this exit is only useful for instrumented applications.

