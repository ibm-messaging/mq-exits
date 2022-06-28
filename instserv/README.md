This directory contains implementations of Installable Services.

The only one that is used these days is the Authorisation Service. The
IBM MQ product includes the Object Authority Manager (OAM) as its default
implementation of this interface, but alternative implementations
can be used to extend or replace the OAM.

## Contents

* oamlog  - Create a logfile of all calls made to the Authorisation Service.
* oamcrt  - Demonstrate how creation of objects can be controlled with granular
            permissions based on the object name
* oamok   - Report successful authentications and authorisations in a 
            JSON-formatted log
