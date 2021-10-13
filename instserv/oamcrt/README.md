# OAMCRT
An sample Installable Service for IBM MQ that shows how to apply controls to which objects
can be created by a user.

## Overview
In normal operation, permissions can be granted to allow someone to create
an object such as a queue. But there are no granular rules to say what the
name of the queue can be - you can either create a queue or not. This is usually
fine because creation of queues is an administrative operation and regular users
should not be allowed to do it.

This module shows how it is possible to extend MQ's authorisation model to apply
such rules. Perhaps you want to provide a system where multiple developers have
some control over their own namespace.

It can control both normal creation of an object, and the creation of a dynamic
queue when a model is opened.

This package does not actually implement sophisticated rules. Instead it has
a very simple rule. If the object being created starts with the string **NOTALLOWED**
then the creation is rejected. But there are functions showing where we would add some kind
of configuration and pattern matching for a more realistic operation. See the
`permittedObject` and `readConfig` functions for where that would fit.

## Building
There is a Makefile provided for Linux platforms. The code is written and
tested only on Linux, though it could be adapted fairly easily for other platforms
provided the compiler supports thread-local storage (the `__thread` attribute in gcc).

## Configuration
Edit `/var/mqm/qmgrs/<qmgr>/qm.ini` after creation of
the queue manager and add the following stanza BEFORE the regular OAM's
stanza (the one with amqzfu in it). The order is important as the queue manager
loads modules in the listed sequence and we need this module to be called before
the OAM.

```
ServiceComponent:
  Service=AuthorizationService
  Name=CreationExtensionAuth
  Module=/var/mqm/exits64/oamcrt_r
  ComponentDataSize=0
```

After updating the qm.ini file, stop and restart the queue manager for this
new module to be recognised.

## Verification
The `RUNME.sh` script will drive the compilation and run a few simple tests against
the named queue manager.

There is a slightly-modified version of the amqsput program included here which makes
it easier to use default values of some of its command-line parameters. It is used
so we can test the use of a MODEL queue, where the name of the created dynamic queue
is given.

## Example output
From a `runmqsc` session:

```
Starting MQSC for queue manager OAMLOG.
     1 : DEF QL(NOTALLOWED.1) REPLACE
AMQ8135E: Not authorized.
One valid MQSC command could not be processed.
```

## Background
The sequences of operations called by the queue manager were initially recorded
using the OAMLOG module, also in this repository.

### Normal "create" (eg DEF QL)
  [28966972.2828 @ Wed Oct 13 09:12:40 2021] OACheckAuth
        Object  : "Z                                               " [Queue]
        User    : "metaylor"
        Auth    : 0x00010000 [crt ]

### Implicit creation by opening a model queue
  [28966972.2829 @ Wed Oct 13 09:47:24 2021] OACheckAuth
        Object  : "SYSTEM.DEFAULT.MODEL.QUEUE                      " [Queue]
        User    : "metaylor"
        Auth    : 0x00000008 [put ]
  [28966972.2829 @ Wed Oct 13 09:47:24 2021] OACheckAuth
        Object  : "SYSTEM.DEFAULT.MODEL.QUEUE                      " [Model Queue]
        User    : "metaylor"
        Auth    : 0x00040000 [dsp ]
  [28966972.2829 @ Wed Oct 13 09:47:24 2021] OACopyAllAuth
        From    : "SYSTEM.DEFAULT.MODEL.QUEUE                      " [Queue]
        To      : "AMQ.616694CC23EA03F6                            "
  [28966972.2829 @ Wed Oct 13 09:47:24 2021] OAGetExplicitAuth
        Object  : "AMQ.616694CC23EA03F6                            " [Queue]
        User    : "metaylor"
  [28966972.2829 @ Wed Oct 13 09:47:24 2021] OASetAuth
        Object  : "AMQ.616694CC23EA03F6                            " [Queue]
        User    : "metaylor"
        Auth    : 0x02FF3FFF [allmqi crt alladmsystem ]
  [28966972.2829 @ Wed Oct 13 09:47:24 2021] OASetAuth
        Object  : "AMQ.616694CC23EA03F6                            " [Queue]
        Group   : "mqm"
        Auth    : 0x02FF3FFF [allmqi crt alladmsystem ]
