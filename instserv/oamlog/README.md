# OAMLOG
A sample Authorisation service for IBM MQ

## Overview

This module implements the IBM MQ Authorisation Service API,
defined [here](https://www.ibm.com/support/knowledgecenter/SSFKSJ_9.2.0/com.ibm.mq.ref.dev.doc/q110070_.htm).

It also explains some of what actually happens when the interface is
invoked, and expands on the information provided in the documentation.

It tracks requests made to a real authorisation component such as the
Object Authority Manager (OAM, contained in the amqzfu module) shipped
with MQ, writing a log as it proceeds.  It does not make any
authorisation checks itself.  A log may be useful to debug authorisation
problems as you can see exactly what request (in particular, which
userid) is being checked by the OAM.  Without the log you might have to
look at MQ trace files or worse.

As it is supplied in source format, it could be extended and used, like
the IBM supplied OAM, to provide a full authorisation capability.

Many extensions have made to the Authorisation Service API since the original version
of this code was released. This version of the module exposes those
enhancements. In particular you can now see how to authenticate, as well
as authorise, applications that are connecting to the queue manager.

In preparation for its release on GitHub, various `#ifdef` flags for older
versions of the interface have been removed as those versions of MQ are no
longer in support.

## Building

The code needs to be built as a threaded, dynamically-loaded module with
MQStart as the entrypoint. A Makefile is included here for Linux;
instructions for other platforms can be found in the MQ documentation. Simply
run `make` to create the module.

## Configuration

On Unix platforms, edit `/var/mqm/qmgrs/<qmgr>/qm.ini` after creation of
the queue manager and add the following stanza BEFORE the regular OAM's
stanza (the one with amqzfu in it). The order is important as the queue manager
loads modules in the listed sequence and we need this module to be the
first one called.

```
ServiceComponent:
  Service=AuthorizationService
  Name=Auditing.Auth.Service
  Module=/var/mqm/exits64/oamlog
  ComponentDataSize=0
```

After updating the qm.ini file, stop and restart the queue manager for this
new module to be recognised.

## Verification
A simple `RUNME.sh` checks the configuration and runs a few commands against
a queue manager called `OAMLOG` so you can see the output.

## Example output
The logfile created will look something like

```
[16890.16890 @ Tue Aug 25 12:57:49 2020] OAInit
	QMgr    : "OAMLOG                                          "
	CC      : 0  	RC      : 0
	CompSize: 0
	Options : 0x00000001 [Secondary]
[16890.16890 @ Tue Aug 25 12:57:49 2020] OACheckAuth
	Object  : "OAMLOG                                          " [QMgr]
	User    : "metaylor"
	Auth    : 0x02000001 [connect system ]
[16890.16890 @ Tue Aug 25 12:57:49 2020] OACheckAuth
	Object  : "OAMLOG                                          " [QMgr]
	User    : "metaylor"
	Auth    : 0x00000020 [set ]
[16890.16890 @ Tue Aug 25 12:57:49 2020] OACheckAuth
	Object  : "SYSTEM.AUTH.DATA.QUEUE                          " [Queue]
	User    : "metaylor"
	Auth    : 0x0000002E [browse get put set ]
[16890.16890 @ Tue Aug 25 12:57:49 2020] OACheckAuth
	Object  : "SYSTEM.DEFAULT.PROCESS                          " [Process]
	User    : "metaylor"
	Auth    : 0x00040000 [dsp ]

```

## Notes:

* I spell 'Authorisation' with an 's'!

* It's quite possible for output from separate processes/threads to be
interleaved in the log file. This should not be too confusing if it
happens though. You might like to add thread-id info to the logs. You
could also add locks around the printing code.

* We can only track requests to, but not the responses from, the real OAM
as the chained services are only called until one returns MQCC_OK; the
rest of the chain is then ignored so we wouldn't be called. So we can't
log whether the OAM actually approved or rejected a call.

* Configuration of this module is a manual process and can only be done
after a queue manager has been created (which would normally setup the
default OAM).

* I haven't tried to split out separate logfiles for each qmgr, or to log
the qmgr name in each request as that can be deduced from the
appropriate pid where you'll see a 'connect' request come through. You
will never have one process servicing requests from two different queue
managers. The logfile will grow indefinitely unless you do something to
truncate it occasionally.

* Do not change the input parameters and expect them to get passed to the
next service in the chain.

* There are several versions of the Authorisation Service interface; the
service component can select which level it supports. In particular,
there are a few "_2" variants of the functions. We will be using those
so that we can print out the Windows user information. The MQZED
structure is supported on Unix platforms too, but the Windows-specific
fields are not then filled in by the qmgr.

* Readability of the source code has been put ahead of its performance.

## Change History

* 12 Jan 2001   Initial Release as an independent SupportPac
* 24 Mar 2006   Updated for WMQ V5.3 and V6
* 18 Jun 2009   Updated for WMQ V7 and moved to SupportPac MS0P
* 07 Sep 2011   Updated for WMQ V7.1 object types
* 09 May 2012   Updated for WMQ V7.1 relocatability: compile/link flags
* 25 Aug 2020   Updated and reformatted for a github repository release.
