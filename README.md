# mq-exits

This repository contains source code and related material for a set
of IBM MQ exits.

MQ has various types of exit points that can be used to extend the product
functionality, and examples of some of them are included here.

## Documentation

More information about exit interfaces can be found starting at
[this section](https://www.ibm.com/docs/en/ibm-mq/latest?topic=reference-user-exits-api-exits-installable-services) of the IBM Docs.

## Prerequisites

* For the Distributed platforms, most exits are written in C. You must have a C compiler.
* For the JMS JWT exit, see required libraries in the [jmsjwtexit README](https://github.com/ibm-messaging/mq-exits/blob/master/channel/jmsjwtexit/README.md).
* For the OTel API Exit, you need a C++ compiler and be able to build the OpenTelemetry CPP SDK libraries

## Health Warning

This package is provided as-is with no guarantees of support or updates. There are
also no guarantees of compatibility with any future versions of IBM MQ .

## History

See [CHANGELOG](CHANGELOG.md) in this directory.

## Issues and Contributions

For feedback and issues relating specifically to this package, please use
the [GitHub issue tracker](https://github.com/ibm-messaging/mq-exits/issues).

Contributions to this package can be accepted under the terms of the Developer's Certificate
of Origin, found in the [DCO file](DCO1.1.txt) of this repository. When
submitting a pull request, you must include a statement stating you accept the terms
in the DCO.

## Copyright

Copyright IBM Corporation 2020,2024
