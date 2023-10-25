# EXTJWTEXIT

A sample security exit for authenticating with JWT Tokens issued by a third-party issuer. In this demo, the third-party issuer is KeyCloak.

## Overview
In MQ 9.3.3, a new authentication mechanism has been added into IBM MQ - authenticating via JWT Tokens issued by a third-party issuer. This sample implements a security exit to provide token authentication capability without making changes to the original messaging applications. 

When called as a security exit on a Client-Connection channel, the sample will connect to an external token issuer server, retrieve a JWT Token, and adopt a pre-configured user from the Token to authenticate to the IBM MQ Queue Manager. The exit will query a token from the token endpoint using CURL, and parse the response to obtain the token to be added into the MQCSP.

## Building
The code needs to be built as a 64-bit threaded, dynamically-loaded module with ChlExit as the entrypoint. When building, you must have CURL and JSON installed on your machine, and these libraries must be linked during the build. Full instructions on building channel exits can be found in the MQ documentation.

Once built the exit must be copied into the MQ exits64 directory located under the MQ Data Directory (Commonly /var/mqm for UNIX).

## Configuration

Below are sample steps and queue manager configuration to run the JWT Token demo.

On your Linux machine or environment, with MQ installed:
1. Install curl + json sudo yum install libcurl
   sudo yum install libcurl-devel
   sudo yum install json-c-devel
2. Download a copy of the security exit, and, as root, build using:
	 gcc -m64 -shared -fPIC -o /var/mqm/exits64/extjwtexit ./extjwtexit.c -I/opt/mqm/inc -ljson-c -lcurl
3. Create a queue manager and configure appropriately:
   1. crtmqm DEMO
   2. strmqm DEMO
   3. runmqsc DEMO
			1. ALTER QMGR CHLAUTH(DISABLED)
			2. DEFINE LISTENER(LIST1) TRPTYPE(TCP) PORT(1513) CONTROL(QMGR)
			3. START LISTENER(LIST1)
			4. DEFINE CHANNEL(CHAN1) CHLTYPE(SVRCONN) SSLCAUTH(OPTIONAL)
			5. DEFINE QLOCAL(Q1)
4. Go into the QMGR SSL directory and configure the certificates to connect to the external AuthToken server:
			1. cd /var/mqm/qmgrs/DEMO/ssl
			2. runmqakm -keydb -create -db tokens.kdb -pw wibble
			3. Retrieve the certificates and add them into the certificate keystore. There are different ways in which you can retrieve the certificates, one way would be by issuing a CURL GET command on the AuthToken server and retrieving the appropriate RSA256 certificate. Copy the certificate into a separate file, e.g. keycloak.cer (surround the certificate with -----BEGIN CERTIFICATE---- and -----END CERTIFICATE----- tags).
			4. Add the certificate into the keystore.
         runmqakm -cert -add -db tokens.kdb -pw wibble -file keycloak.cer -label ioWTxuAYAQui10E19s92LJP9lvGsJbhn7coWvVw6ASE
			5. Encrypt the keystore password using the runqmcred command and save into a separate file, e.g. [keystore.pw](https://keystore.pw)
			6. Give the keystore necessary permissions:
         chmod g+r /var/mqm/qmgrs/DEMO/ssl/tokens.kdb
 5. Configure the queue manager qm.ini config file to specify the token issuer information for token validation.
      1. cd /var/mqm/qmgrs/DEMO/
      2. vi qm.ini
      3. Add the new AuthToken stanza with the required information. For example:
				 AuthToken:
	         KeyStore=/var/mqm/qmgrs/DEMO/ssl/tokens.kdb
				   KeyStorePwdFile=/var/mqm/qmgrs/DEMO/ssl/keystore.pw
					 CertLabel=ioWTxuAYAQui10E19s92LJP9lvGsJbhn7coWvVw6ASE
				   UserClaim=preferred_username
6. Issue a REFRESH SECURITY or restart the queue manager via endmqm/strmqm. 
7. Set the correct authority records on the queue manager. The Principal should be set to that of the incoming user from the JWT Token. The SCYDATA attribute when defining the CLNTCONN channel can be used to specify, how chatty the output should be - if set to DEBUG, the output will be more chatty.
	 1. SET AUTHREC OBJTYPE(QMGR) PRINCIPAL('admin') AUTHADD(CONNECT)
	 2. SET AUTHREC PROFILE(Q1) OBJTYPE(QUEUE) PRINCIPAL('admin') AUTHADD(ALLMQI)
	 3. DEFINE CHANNEL(CHAN1) CHLTYPE(CLNTCONN) CONNAME('127.0.0.1(1513)') QMNAME(DEMO) SCYEXIT('extjwtexit(ChlExit)') SCYDATA(DEBUG)
8. Set the appropriate environment variables for the exit:  
	 export MQCHLLIB=/var/mqm/qmgrs/DEMO/@ipcc
   export MQCHLTAB=AMQCLCHL.TAB
   export JWT_TOKEN_ENDPOINT=<https://tokenendpoint.com>:/realms/master/protocol/openid-connect/token
   export JWT_TOKEN_USERNAME=admin
   export JWT_TOKEN_PWD=tokenuserpassword
   export JWT_TOKEN_CLIENTID=admin-cliendid
9. Run the amqsputc sample to confirm that the exit is running and working:
   /opt/mqm/samp/bin/amqsputc Q1 DEMO

## Example Output

[vas@dupe1 DEMO]$ /opt/mqm/samp/bin/amqsputc Q1 DEMO
Sample AMQSPUT0 start
> Obtaining token from endpoint 'https:[tokenserver.com](https://tokenserver.com):/realms/master/protocol/openid-connect/token' with User 'admin'
> Global curl init
> curl init
> connecting!
Got back a token response!
Token to be used:
eyJhbGciOiJSUzI1NiIsInR5cCIgOiAiSldUIiwia2lkIiA6ICJpb1dUeHVBWUFRdWkxMEUxOXM5MkxKUDlsdkdzSmJobjdjb1d2Vnc2QVNFIn0.eyJleHAiOjE2ODYxMzQwOTIsImlhdCI6MTY4NjEzNDAzMiwianRpIjoiNTgwODdiYTgtMDM3NC00NmM5LTgyYTYtOGRlOTBjNzEyZDFmIiwiaXNzIjoiaHR0cHM6Ly9rZXljbG9hay0yMC0wLTMubXFvcHMtc2VydmljZXMuaHVyc2xleS5pYm0uY29tL3JlYWxtcy9tYXN0ZXIiLCJzdWIiOiJjYjY0MjllMi1iYjhkLTQ5MTYtOGU1NS1mNGU5NzA3YzhhNTIiLCJ0eXAiOiJCZWFyZXIiLCJhenAiOiJhZG1pbi1jbGkiLCJzZXNzaW9uX3N0YXRlIjoiNzMyNjg1OTUtNDQ2ZC00YTY4LTg1ZTctYWFkMDE1Nzg1ODkzIiwiYWNyIjoiMSIsInNjb3BlIjoiZW1haWwgcHJvZmlsZSIsInNpZCI6IjczMjY4NTk1LTQ0NmQtNGE2OC04NWU3LWFhZDAxNTc4NTg5MyIsImVtYWlsX3ZlcmlmaWVkIjpmYWxzZSwicHJlZmVycmVkX3VzZXJuYW1lIjoiYWRtaW4ifQ.N3jf_kLoIK9eAVr0op6fjPF08CdsVe4GsGz9SVTBfyKLT5H9JQiPOt53-n2pBu8zpMpGxvBRCLidSrBMvpIW0NIpruD4rxN-Nu0zriTybXA1vz6GX5z4xNroIg_DPUG9nq4FUXIzxn7BBCWH_5H9nF8P_upET7SyrBfHCx2XD2Ot3NFidLVj6lx3N_bZllLa9MKfwvv9N0R_7vPM2Uw9YkWFtHjpZRpu-hp2iBEVOcgPIK6F7Uslr6JyJpKfYcdzmO14qNmxhAizCQdABy-T3RdvN15cW7hVe22bda2kFA0OotO13VICW_dNlfbqD95QINGsaI0qu3MK7P_ZQ4Qybw
target queue is Q1
