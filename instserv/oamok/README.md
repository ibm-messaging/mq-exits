# OAMOK
An Authorisation service extension for IBM MQ 9.3

## Overview

This module implements the IBM MQ Authorisation Service API,
defined [here](https://www.ibm.com/support/knowledgecenter/SSFKSJ_9.2.0/com.ibm.mq.ref.dev.doc/q110070_.htm).

It is designed to record authentication and authorisation requests that have
already passed the checks made by the standard authorisation component, the OAM.
It can therefore be used as an auditing capability of who has successfully accessed
queue manager resources.

Unsuccessful requests are not reported by this component because there are
already product-provided mechanisms to collect that data. For example, in
error logs or in Authorisation Event messages.

The output is written in JSON format, to simplify feeding the information to
various analytics tools

## Building

The code needs to be built as a threaded, dynamically-loaded module with
MQStart as the entrypoint. A Makefile is included here for Linux and AIX;
instructions for other platforms can be found in the MQ documentation. Simply
run `make -f <Makefile> oamok` to create the module.

The code requires a minimum of MQ 9.3.0 in order to execute correctly. So I
have stopped it building against lower levels.

You must have the json-c library installed both to compile and to run the
code. The header files needed for compilation are typically part
of a separate package. For example,

```
dnf install json-c
dnf install json-c-devel
```

You might need to adjust pathnames in the Makefiles to point at where the json-c
files have been installed. The supplied Makefiles match my AIX and my Fedora
images.


## Configuration

On Unix platforms, edit `/var/mqm/qmgrs/<qmgr>/qm.ini` after creation of
the queue manager and add the following stanza AFTER the regular OAM's
stanza (the one with amqzfu in it). The order is important as the queue manager
loads modules in the listed sequence and we need this module to be the
last one called.

```
ServiceComponent:
  Service=AuthorizationService
  Name=Auditing.Auth.Service
  Module=/var/mqm/exits64/oamok
  ComponentDataSize=0
```

After updating the qm.ini file, stop and restart the queue manager for this
new module to be recognised.

### Options
The JSON output is written by default as a single line as that is how consuming
tools tend to prefer it. To switch to a pretty-printed multi-line format, set
the AMQ_OAMAUDIT_MULTILINE environment variable to any non-null value before starting
the queue manager.

## Verification
A simple `RUNME.sh` checks the configuration and runs a few commands against
a queue manager called `OAMLOG` so you can see the output.

## Example output
The logfile created will look something like this (lines split for clarity)

```
{ "action":"authorise", queueManager:"OAMLOG",
  "timeEpoch":1649752243,"timeString":"2022-04-12 09:30:43 BST",
  "objectType":"qMgr","objectName":"OAMLOG",
  "identity":"metaylor",
  "authorityHex":"0x02000001","authorityString":["connect","system"],
  "connCorrel":"481282.481282"
}

{
  "action":"authenticate",
  "timeEpoch":1649756145, "timeString":"2022-04-12 10:35:45 BST",
  "queueManager":"OAMLOG",
  "identity":"admin", "applicationName":"amqsput",
  "environment":"other",
  "caller":"external",
  "cspAuthenticationType":"password",  "cspUserId":"admin",
  "authenticationContext":"initialContext",
  "bindType":"sharedBinding",
  "applicationPid":504146,  "applicationTid":1,
  "connCorrel":"504131.504156"
}

```

## Notes

* I spell 'Authorisation' with an 's'!

* Configuration of this module is a manual process and can only be done
after a queue manager has been created (which would normally setup the
default OAM).

* The information reported is all that is available to the OAM. In particular
it cannot see information such as the name of the channel associated with
a client connection. But you can see from the `environment` element when any
authentication was associated with a client. It will have the value `mcaSvrConn`.

* If the queue manager is using LDAP-based authorisations, then we do not
report on the mapping to the full DN unless it happens to be used as the
cspUserId in an authentication operation. The derived shortname is what gets used
later for authorisation checking.

* The `connCorrel` value in the output allows you to link all operations for
a given connection. While connected, the application will have all its
authorisation work done using the same correlator value. After disconnection, the
`connCorrel` value might be reused, but you can tell that a new application has started
by seeing an authentication request, or an authorisation request for the queue manager
with a `connect` authority.

* This module deliberately ignores any calls where the Reason is already non-zero. While
some failed authorisation requests may be passed to a later chained OAM, not all are. So
rather than try to report on SOME failures, we ignore them all completely. You can see
failed requests elsewhere such as in the error log or event messages.

* This package has been tested using the regular OAM. Other authorisation modules might not
set flags suitably for this component can be called.

* Other things that you might like to see as audited include explicit SETTING of authorities. That
can already be done with command/configuration events.

* The name of the report file is hardcoded to /var/mqm/audit/oamok.log. Change the code
to change the file name.

* Although there's a little bit of Windows-specific code in the module, it is not complete
and more work would be needed to build and run it on that platform.

* The logfile will grow until the queue manager is ended or you take action to remove it;
for example you could use the logrotate package.

### A logrotate configuration
This configuration can be put in a file in `/etc/logrotate.d`.

```
/var/mqm/audit/oamok.log {
    missingok
    daily  
    create 0660 mqm mqm  
    su mqm mqm
    minsize 1M
    rotate 3
    copytruncate
}

```

If you have SELinux enabled, then you may also need
to permit the logrotate context to work within the directory.

```
semanage fcontext -a -t var_log_t /var/mqm/audit
restorecon -v /var/mqm/audit
```

## Possible enhancements
* Externalise the configuration such as the name of the output file and the multi or single-line output
* Split the output file based on queue manager name
* Add an option to not record administrator authorisation requests (something like "if getruid() == identity") as
many checks are done for internal queue manager operations. 

## Linked blog post
I wrote up more about this at https://marketaylor.synology.me/?p=1254

## Change History
* 27 June 2022  Initial Release
