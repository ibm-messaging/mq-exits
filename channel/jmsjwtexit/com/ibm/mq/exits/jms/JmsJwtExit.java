/*
* (c) Copyright IBM Corporation 2023
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

package com.ibm.mq.exits.jms;

import java.util.Map;

import java.nio.ByteBuffer;

import com.ibm.mq.exits.WMQSecurityExit;
import com.ibm.mq.exits.MQCXP;
import com.ibm.mq.exits.MQCD;
import com.ibm.mq.exits.MQCSP;
import com.ibm.mq.constants.CMQXC;
import com.ibm.mq.constants.CMQC;

import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpRequest.BodyPublishers;
import java.net.http.HttpResponse;

import java.util.logging.*;

import org.json.JSONObject;

public class JmsJwtExit implements WMQSecurityExit {
  private static Logger logger = Logger.getLogger("com.ibm.mq.exits.jms");
    public JmsJwtExit() {
    }

  // This method implements the security exit interface
  public ByteBuffer channelSecurityExit(MQCXP channelExitParms,
                                        MQCD channelDefinition,
                                        ByteBuffer agentBuffer) {
    logger.fine("starting the exit");
    // Complete the body of the security exit here
    int exitReason = channelExitParms.getExitReason();
    switch (exitReason) {
      case CMQXC.MQXR_INIT:
        logger.info("1.MQXR_INIT");
        break;
      case CMQXC.MQXR_INIT_SEC:
        logger.info("2.MQXR_INIT_SEC");
        break;
      case CMQXC.MQXR_SEC_PARMS:
        logger.info("3.MQXR_SEC_PARMS");
        MQCSP mycsp = channelExitParms.getSecurityParms();
        if (mycsp == null) {
          mycsp = channelExitParms.createMQCSP();
          logger.info("4.creating CSP");
          }
          try {
            mycsp.setAuthenticationType(CMQC.MQCSP_AUTH_ID_TOKEN);
            mycsp.setVersion(3);
            mycsp.setToken(obtainToken());
            logger.info("5.set obtained token");
            channelExitParms.setSecurityParms(mycsp);
          } catch (Exception ex) {
            ex.printStackTrace();
          }
          break;
        case CMQXC.MQXR_TERM:
          logger.info("6.MQXR_TERM");
          break;
        default:
          break;
      }
      logger.info("Calling the Java exit");
      return agentBuffer;
    }

    public static String obtainToken() {
      String access_token = "";
      String tokenEndpoint = System.getenv("JWT_TOKEN_ENDPOINT");
      String tokenUsername = System.getenv("JWT_TOKEN_USERNAME");
      String tokenPassword = System.getenv("JWT_TOKEN_PWD");
      String tokenClientId = System.getenv("JWT_TOKEN_CLIENTID");

      //build the string with parameters provided as environment variables, to include in the POST request to the token issuer
      String postBuild = String.format("client_id=%s&username=%s&password=%s&grant_type=password",tokenClientId, tokenUsername, tokenPassword);
      logger.info("parameter string: " + postBuild);
      HttpClient client = HttpClient.newHttpClient();

      HttpRequest request = HttpRequest.newBuilder()
          .uri(URI.create(
  //            "<keycloak URL>/realms/master/protocol/openid-connect/token"
              tokenEndpoint
              ))
          .POST(
              BodyPublishers.ofString(postBuild))
          .setHeader("Content-Type", "application/x-www-form-urlencoded")
          .build();
          logger.info("obtaining token from:" + tokenEndpoint);
      try {
        HttpResponse<String> response = client.send(request, HttpResponse.BodyHandlers.ofString());
        logger.fine(String.valueOf(response.statusCode()));
        logger.fine(response.body());

        JSONObject myJsonObject = new JSONObject(response.body());
        access_token = myJsonObject.getString("access_token");
        logger.info("Using token:" + access_token);
      } catch (Exception e) {
        e.printStackTrace();
      }
      return access_token;
    }
  }
