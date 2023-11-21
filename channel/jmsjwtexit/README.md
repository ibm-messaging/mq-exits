# JmsJwtExit

A sample security exit for authenticating with JWT tokens issued by a third-party issuer. In this demo, the third-party issuer is a KeyCloak server.
For details on using JWT tokens with MQ, see [Using authentication tokens in an application](https://www.ibm.com/docs/en/ibm-mq/latest?topic=tokens-using-authentication-in-application).

## Overview
In MQ 9.3.4, a new authentication mechanism has been added into IBM MQ - authenticating with JWT tokens that are issued by a third-party trusted issuer. This sample implements a security exit to provide token authentication capability without making changes to the original messaging application. 

When called as a security exit on a client-connection channel, the sample connects to an trusted token issuer server, retrieves a JWT Token, and adopts a pre-configured user from the token to authenticate to the IBM MQ Queue Manager. The exit will query a token from the token endpoint using CURL, and parse the response to obtain the token to be added into the MQCSP.

## Building
To build the exit, include the paths to the MQ allclient and jms-api jars. You can use the jars in the MQ /opt/mqm/java/lib directory. Alternatively, get the [IBM MQ allclient.jar](https://mvnrepository.com/artifact/com.ibm.mq/com.ibm.mq.allclient) and the [jms-api.jar](https://mvnrepository.com/artifact/javax.jms/javax.jms-api/2.0.1) from Maven and include the paths to where they are saved in the javac command. 

```
javac -cp /opt/mqm/java/lib/com.ibm.mq.allclient.jar:./javax.jms-api-2.0.1.jar com/ibm/mq/exits/jms/JmsJwtExit.java
```

To build the sample application cd to the directory that contains the sample package and include the [IBM MQ allclient.jar](https://mvnrepository.com/artifact/com.ibm.mq/com.ibm.mq.allclient), [jms-api.jar](https://mvnrepository.com/artifact/javax.jms/javax.jms-api/2.0.1) and the [org.json.jar](https://mvnrepository.com/artifact/org.json/json).
```
javac -cp ./com.ibm.mq.allclient.jar:./javax.jms-api-2.0.1.jar:./json-20220320.jar com/ibm/mq/samples/jms/JmsPutGet.java
```

Once built, the exit JmsJwtExit.class file must be copied into the MQ exits64 directory located under the MQ Data Directory (commonly /var/mqm for UNIX).

## Configuration

Configure the queue manager to run the JWT token demo.

On your Linux machine or environment, with IBM MQ installed:

1. Create a queue manager and configure appropriately:
   1. crtmqm DEMO
   2. strmqm DEMO
   3. runmqsc DEMO
   ```
	DEFINE LISTENER(LIST1) TRPTYPE(TCP) PORT(1513) CONTROL(QMGR)
	START LISTENER(LIST1)
	DEFINE CHANNEL(CHAN1) CHLTYPE(SVRCONN) SSLCAUTH(OPTIONAL)
	DEFINE QLOCAL(Q1)
    ```
2. Go into the QMGR SSL directory and configure the certificates to connect to the external AuthToken server:
```
cd /var/mqm/qmgrs/DEMO/ssl
runmqakm -keydb -create -db tokens.kdb -pw wibble
```
3. Retrieve the certificates and add them into the certificate keystore. There are different ways in which you can retrieve the certificates, one way would be by issuing a CURL GET command on the AuthToken server and retrieving the appropriate RSA256 certificate. 
For example:

```
curl -k -X GET <keycloak-server-url>/realms/master/protocol/openid-connect/certs
```

From the "alg": "RS256" portion of the fetched JSON file, copy the string value from the field "x5c".

Paste this string into a separate file, e.g. keycloak.cer 
(surround the certificate string with begin and end certificate tags):
```
-----BEGIN CERTIFICATE---- 
<paste certificate string here and save the file>
-----END CERTIFICATE----- 
```

4. Add the certificate into the keystore.
```
runmqakm -cert -add -db tokens.kdb -pw wibble -file keycloak.cer -label label1
```
5. Encrypt the keystore password using the runqmcred command and save into a separate file, e.g. keystore.pw
See Step 2. of the [Configuring a queue manager to accept authentication tokens](https://www.ibm.com/docs/en/ibm-mq/9.3?topic=tokens-configuring-queue-manager-accept-authentication) task in IBM Docs for more information on encrypting the keystore password.

6. Give the keystore necessary permissions:
```         
chmod g+r /var/mqm/qmgrs/DEMO/ssl/tokens.kdb
```
7. Configure the queue manager qm.ini file to specify the token issuer information for token validation.
```
cd /var/mqm/qmgrs/DEMO/
vi qm.ini
``` 
8. Add the new AuthToken stanza with the required information. For example:
```
AuthToken:
	KeyStore=/var/mqm/qmgrs/DEMO/ssl/tokens.kdb
	KeyStorePwdFile=/var/mqm/qmgrs/DEMO/ssl/keystore.pw
	CertLabel=label1
	UserClaim=preferred_username
```
9. Issue a REFRESH SECURITY or restart the queue manager via endmqm/strmqm. 
10. Set the correct authority records on the queue manager. The Principal should be set to that of the incoming user from the JWT Token. The SCYDATA attribute when defining the CLNTCONN channel can be used to specify, how chatty the output should be - if set to DEBUG, the output will be more chatty.
```
SET AUTHREC OBJTYPE(QMGR) PRINCIPAL('admin') AUTHADD(CONNECT)
SET AUTHREC OBJTYPE(QMGR) PRINCIPAL('admin') AUTHADD(INQ)
SET AUTHREC PROFILE(Q1) OBJTYPE(QUEUE) PRINCIPAL('admin') AUTHADD(ALLMQI)
DEFINE CHANNEL(CHAN1) CHLTYPE(CLNTCONN) CONNAME('127.0.0.1(1513)') QMNAME(DEMO) SCYEXIT('com.ibm.mq.exits.jms.JmsJwtExit') SCYDATA(DEBUG)
```
11. Set the appropriate environment variables for the exit: 
``` 
export MQCHLLIB=/var/mqm/qmgrs/DEMO/@ipcc
export MQCHLTAB=AMQCLCHL.TAB
export JWT_TOKEN_ENDPOINT=<keycloak server URL>/realms/master/protocol/openid-connect/token
export JWT_TOKEN_USERNAME=admin
export JWT_TOKEN_PWD=tokenuserpassword
export JWT_TOKEN_CLIENTID=admin-clientid
```
12. Run the JmsPutGet sample to confirm that the exit is running and working:
```
java -Dcom.ibm.mq.cfg.ClientExitPath.JavaExitsClasspath=/var/mqm/exits64 -cp /opt/mqm/java/lib/com.ibm.mq.allclient.jar:./javax.jms-api-2.0.1.jar:./json-20220320.jar:. com.ibm.mq.samples.jms.JmsPutGet
```
## Example Output
```
oljica@oljica1:~$ java -Dcom.ibm.mq.cfg.ClientExitPath.JavaExitsClasspath=/var/mqm/exits64 -cp /opt/mqm/java/lib/com.ibm.mq.allclient.jar:./javax.jms-api-2.0.1.jar:./json-20220320.jar:. com.ibm.mq.samples.jms.JmsPutGet
Nov 08, 2023 7:52:21 AM com.ibm.mq.exits.jms.JmsJwtExit channelSecurityExit
INFO: 1.MQXR_INIT
Nov 08, 2023 7:52:21 AM com.ibm.mq.exits.jms.JmsJwtExit channelSecurityExit
INFO: Calling the Java exit
Nov 08, 2023 7:52:21 AM com.ibm.mq.exits.jms.JmsJwtExit channelSecurityExit
INFO: 2.MQXR_INIT_SEC
Nov 08, 2023 7:52:21 AM com.ibm.mq.exits.jms.JmsJwtExit channelSecurityExit
INFO: Calling the Java exit
Nov 08, 2023 7:52:21 AM com.ibm.mq.exits.jms.JmsJwtExit channelSecurityExit
INFO: 3.MQXR_SEC_PARMS
Nov 08, 2023 7:52:21 AM com.ibm.mq.exits.jms.JmsJwtExit channelSecurityExit
INFO: 4.creating CSP
Nov 08, 2023 7:52:21 AM com.ibm.mq.exits.jms.JmsJwtExit obtainToken
INFO: parameter string: client_id=admin-cli&username=admin&password=tokenuserpassword&grant_type=password
Nov 08, 2023 7:52:21 AM com.ibm.mq.exits.jms.JmsJwtExit obtainToken
INFO: obtaining token from:<keycloak server URL>/realms/master/protocol/openid-connect/token
Nov 08, 2023 7:52:21 AM com.ibm.mq.exits.jms.JmsJwtExit obtainToken
INFO: Using token:eyJhbGciOiJSUzI1NiIsInR5cCIgOiAiSldUIiwia2lkIiA6ICIyYk5kRjVISFVCZFdXdTdCU2hnampWLTlhZk..TIa71XgtYzw
Nov 08, 2023 7:52:21 AM com.ibm.mq.exits.jms.JmsJwtExit channelSecurityExit
INFO: 5.set obtained token
Nov 08, 2023 7:52:21 AM com.ibm.mq.exits.jms.JmsJwtExit channelSecurityExit
INFO: Calling the Java exit
Nov 08, 2023 7:52:22 AM com.ibm.mq.exits.jms.JmsJwtExit channelSecurityExit
INFO: 3.MQXR_SEC_PARMS
Nov 08, 2023 7:52:22 AM com.ibm.mq.exits.jms.JmsJwtExit channelSecurityExit
INFO: 4.creating CSP
Nov 08, 2023 7:52:22 AM com.ibm.mq.exits.jms.JmsJwtExit obtainToken
INFO: parameter string: client_id=admin-cli&username=admin&password=tokenuserpassword&grant_type=password
Nov 08, 2023 7:52:22 AM com.ibm.mq.exits.jms.JmsJwtExit obtainToken
INFO: obtaining token from:<keycloak server URL>/realms/master/protocol/openid-connect/token
Nov 08, 2023 7:52:22 AM com.ibm.mq.exits.jms.JmsJwtExit obtainToken
INFO: Using token:eyJhbGciOiJSUzI1NiIsInR5cCIgOiAiSldUIiwia2lkIiA6ICIyYk5kRjVISFVCZFdXdTdCU2hnampWLTlhZk..TIa71XgtYzw
Nov 08, 2023 7:52:22 AM com.ibm.mq.exits.jms.JmsJwtExit channelSecurityExit
INFO: 5.set obtained token
Nov 08, 2023 7:52:22 AM com.ibm.mq.exits.jms.JmsJwtExit channelSecurityExit
INFO: Calling the Java exit
Sent message:

  JMSMessage class: jms_text
  JMSType:          null
  JMSDeliveryMode:  2
  JMSDeliveryDelay: 0
  JMSDeliveryTime:  1699458742297
  JMSExpiration:    0
  JMSPriority:      4
  JMSMessageID:     ID:414d512044454d4f2020202020202020d19f4b6501380040
  JMSTimestamp:     1699458742297
  JMSCorrelationID: null
  JMSDestination:   queue:///Q1
  JMSReplyTo:       null
  JMSRedelivered:   false
    JMSXAppID: JmsPutGet (JMS)             
    JMSXDeliveryCount: 0
    JMSXUserID: admin       
    JMS_IBM_PutApplType: 28
    JMS_IBM_PutDate: 20231108
    JMS_IBM_PutTime: 15522232
Your lucky number today is 267

Received message:
Your lucky number today is 267
Nov 08, 2023 7:52:22 AM com.ibm.mq.exits.jms.JmsJwtExit channelSecurityExit
INFO: 6.MQXR_TERM
Nov 08, 2023 7:52:22 AM com.ibm.mq.exits.jms.JmsJwtExit channelSecurityExit
INFO: Calling the Java exit
SUCCESS
```
