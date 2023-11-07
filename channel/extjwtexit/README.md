# EXTJWTEXIT

A sample security exit for authenticating with JWT Tokens issued by a third-party issuer.
For details on JWT tokens and how to use them in your applications, see the [MQ Documentation](https://www.ibm.com/docs/en/ibm-mq/latest?topic=tokens-using-authentication-in-application)

## Overview
In MQ 9.3.4, a new authentication mechanism has been added into IBM MQ - authenticating via JWT Tokens issued by a third-party issuer. This sample implements a security exit to provide token authentication capability without making changes to the original messaging applications. 

When called as a security exit on a Client-Connection channel, the sample will connect to an external token issuer server, retrieve a JWT Token, and adopt a pre-configured user from the Token to authenticate to the IBM MQ Queue Manager. The exit will query a token from the token endpoint using CURL, and parse the response to obtain the token to be added into the MQCSP.

## Building
The code needs to be built as a 64-bit threaded, dynamically-loaded module with ChlExit as the entrypoint. When building, you must have CURL and JSON installed on your machine, and these libraries must be linked during the build. Full instructions on building channel exits can be found in the MQ documentation.

Once built the exit must be copied into the MQ exits64 directory located under the MQ Data Directory (commonly /var/mqm for UNIX).

## Configuration

Below are sample steps and queue manager configuration to run the JWT Token demo.

On your Linux machine or environment, with MQ installed:
1. Install curl + json
```
sudo yum install libcurl
sudo yum install libcurl-devel
sudo yum install json-c-devel
```
2. Download a copy of the security exit, and, as root, build using:
```
gcc -m64 -shared -fPIC -o /var/mqm/exits64/extjwtexit ./extjwtexit.c -I/opt/mqm/inc -ljson-c -lcurl
```
3. Create a queue manager and configure appropriately:
   1. crtmqm DEMO
   2. strmqm DEMO
   3. runmqsc DEMO
   ```
	ALTER QMGR CHLAUTH(DISABLED)
	DEFINE LISTENER(LIST1) TRPTYPE(TCP) PORT(1513) CONTROL(QMGR)
	START LISTENER(LIST1)
	DEFINE CHANNEL(CHAN1) CHLTYPE(SVRCONN) SSLCAUTH(OPTIONAL)
	DEFINE QLOCAL(Q1)
    ```
4. Go into the QMGR SSL directory and configure the certificates to connect to the external AuthToken server:
```
cd /var/mqm/qmgrs/DEMO/ssl
runmqakm -keydb -create -db tokens.kdb -pw wibble
```
5. Retrieve the certificates and add them into the certificate keystore. This will be dependent on your authentication server, but if it exposes a JWKS endpoint (as most OIDC services will), it may be as simple as issuing an HTTP 'GET' from a browser and selecting the appropriate RSA256 certificate from the list returned. Copy the certificate into a separate file, e.g. keycloak.cer (surround the certificate with -----BEGIN CERTIFICATE---- and -----END CERTIFICATE----- tags).
6. Add the certificate into the keystore.
```
runmqakm -cert -add -db tokens.kdb -pw wibble -file keycloak.cer -label label1
```
7. Encrypt the keystore password using the runqmcred command and save into a separate file, e.g. keystore.pw
8. Give the keystore necessary permissions:
```         
chmod g+r /var/mqm/qmgrs/DEMO/ssl/tokens.kdb
```
9. Configure the queue manager qm.ini config file to specify the token issuer information for token validation.
```
cd /var/mqm/qmgrs/DEMO/
vi qm.ini
``` 
10. Add the new AuthToken stanza with the required information. For example:
```
AuthToken:
	KeyStore=/var/mqm/qmgrs/DEMO/ssl/tokens.kdb
	KeyStorePwdFile=/var/mqm/qmgrs/DEMO/ssl/keystore.pw
	CertLabel=label1
	UserClaim=preferred_username
```
11. Issue a REFRESH SECURITY or restart the queue manager via endmqm/strmqm. 
12. Set the correct authority records on the queue manager. The Principal should be set to that of the incoming user from the JWT Token. The SCYDATA attribute when defining the CLNTCONN channel can be used to specify, how chatty the output should be - if set to DEBUG, the output will be more chatty.
```
SET AUTHREC OBJTYPE(QMGR) PRINCIPAL('admin') AUTHADD(CONNECT)
SET AUTHREC PROFILE(Q1) OBJTYPE(QUEUE) PRINCIPAL('admin') AUTHADD(ALLMQI)
DEFINE CHANNEL(CHAN1) CHLTYPE(CLNTCONN) CONNAME('127.0.0.1(1513)') QMNAME(DEMO) SCYEXIT('extjwtexit(ChlExit)') SCYDATA(DEBUG)
```
13. Set the appropriate environment variables for the exit: 
``` 
export MQCHLLIB=/var/mqm/qmgrs/DEMO/@ipcc
export MQCHLTAB=AMQCLCHL.TAB
export JWT_TOKEN_ENDPOINT=.../realms/master/protocol/openid-connect/token
export JWT_TOKEN_USERNAME=admin
export JWT_TOKEN_PWD=tokenuserpassword
export JWT_TOKEN_CLIENTID=admin-cliendid
```
14. Run the amqsputc sample to confirm that the exit is running and working:
```
/opt/mqm/samp/bin/amqsputc Q1 DEMO
```
## Example Output
```
[vas@dupe1 DEMO]$ /opt/mqm/samp/bin/amqsputc Q1 DEMO
Sample AMQSPUT0 start
> Obtaining token from endpoint 'https:tokenserver.com:/realms/master/protocol/openid-connect/token' with User 'admin'
> Global curl init
> curl init
> connecting!
Got back a token response!
Token to be used:
eyJhbGciOiJSUzI1NiIsInR5cCIgOiAiSldUIiwia2lkIiA6ICJpb1dUeHVBWUFRdWkxMEUxOXM5MkxKUDlsdkdzSmJobjdjb1d2Vnc2QVNFIn0...Q4Qybw
target queue is Q1
```
