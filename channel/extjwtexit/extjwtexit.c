/*****************************************************************************/
/*                                                                           */
/* Module Name: extjwtexit.c                                                 */
/*                                                                           */
/* (C) IBM Corporation 2023                                                  */
/*                                                                           */
/* AUTHOR: Vasily Shcherbinin, IBM Hursley                                   */
/*                                                                           */
/*     Licensed under the Apache License, Version 2.0 (the "License");       */
/*     you may not use this file except in compliance with the License.      */
/*     You may obtain a copy of the License at                               */
/*                                                                           */
/*              http://www.apache.org/licenses/LICENSE-2.0                   */
/*                                                                           */
/*     Unless required by applicable law or agreed to in writing, software   */
/*     distributed under the License is distributed on an "AS IS" BASIS,     */
/*     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,                         */
/*     either express or implied.                                            */
/*                                                                           */
/*     See the License for the specific language governing permissions and   */
/*     limitations under the License.                                        */
/*                                                                           */
/* Description: Security exit that takes a Token endpoint, username          */
/*              and password as environment variables to dynamically         */
/*              retrieve a JWT token from a AuthToken issuer and adds it to  */
/*              the MQCSP structure for authentication.                      */
/*                                                                           */
/*****************************************************************************/
/* CURL COPYRIGHT                                                            */
/* COPYRIGHT AND PERMISSION NOTICE                                           */
/*                                                                           */
/* Copyright (c) 1996 - 2023, Daniel Stenberg, <daniel@haxx.se>, and many    */
/* contributors.                                                             */
/*                                                                           */
/* All rights reserved.                                                      */
/*                                                                           */
/* Permission to use, copy, modify, and distribute this software for any     */
/* purpose with or without fee is hereby granted, provided that the above    */
/* copyright notice and this permission notice appear in all copies.         */
/*                                                                           */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR*/
/* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,  */
/* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY       */
/* RIGHTS. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR  */
/* ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE*/ 
/* OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                             */
/*                                                                           */
/* Except as contained in this notice, the name of a copyright holder shall  */
/* not be used in advertising or otherwise to promote the sale, use or other */
/* dealings in this Software without prior written authorization of the      */
/* copyright holder.                                                         */
/*****************************************************************************/

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <curl/curl.h>
#include <json-c/json.h>

#include <cmqc.h>             /* MQI header                          */
#include <cmqxc.h>            /* Installable services header         */

#if MQAT_DEFAULT == MQAT_UNIX
#include <strings.h>
#ifndef stricmp
#define stricmp strcasecmp
#endif
#else
#error Unsupported platform
#endif

#define   SUCCESS              0
#define   FAILURE              1
#define   DEBUG_OPTION         "DEBUG"
#define   CURL_LINKAGE
#define   NULL_POINTER (void *)0

#ifndef FALSE
#define FALSE (0)
#endif
#ifndef TRUE
#define TRUE (1)
#endif

static int debugPrint = FALSE;

/* Function prototypes */
static int AuthTokenLogin(char *TokenEndpoint, char *UserId, char *Password, char *ClientId, char **response);
static int obtainToken(char *TokenEndpoint, char *UserId, char *Password, char* ClientId, char **token);
static int RetrieveTokenFromResponse(char *response, char **token);
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);

/* DISCLAIMER: Code sourced from https://curl.se/libcurl/c/getinmemory.html */
struct MemoryStruct
{
    char *memory;
    size_t size;
};

/* DISCLAIMER: Code sourced from https://curl.se/libcurl/c/getinmemory.html */
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr)
    {
        /* out of memory! */
        fprintf(stderr, "not enough memory (realloc returned NULL)\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

/*****************************************************************/
/* Function to query a token from the token endpoint. Build the  */
/* curl command in AuthTokenLogin and use that to retrieve       */
/* a JSON response from the AuthToken server. Parse the          */
/* response via RetrieveTokenFromResponse to retrieve the token  */
/* to be added into the MQCSP.                                   */
/*****************************************************************/
static int obtainToken(char *TokenEndpoint, char *UserId, char *Password, char* ClientId, char **token)
{
    char *tokenResponse = NULL_POINTER;
    int rc = 0;

    printf("> Obtaining token from endpoint '%s' with User '%s'\n", TokenEndpoint, UserId);
    rc = AuthTokenLogin(TokenEndpoint, UserId, Password, ClientId, &tokenResponse);
    if (rc == 0)
    {
        printf("Got back a token response!\n");
        rc = RetrieveTokenFromResponse(tokenResponse, token);
        memset(tokenResponse, 0, strlen(tokenResponse));
        free(tokenResponse);
    }
    return rc;
}

/*****************************************************************/
/* Parse the response from the AuthToken server containing the   */
/* JWT token in the "access_token" field. This token is later    */
/* passed into the MQCSP structure.                              */
/*****************************************************************/
static int RetrieveTokenFromResponse(char *response, char **token)
{
    json_object *rootObj = NULL;
    json_object *tokenStrObj = NULL;
    char *tokenstr;
    int tokLen = 0;
    json_tokener *tok;
    enum json_tokener_error jerr;
        char *val_type_str, *str, *kid, *cert;
    int val_type, i, i2;

    if (response == NULL_POINTER)
    {
        return -1;
    }

    tok = json_tokener_new();
    rootObj = json_tokener_parse_ex(tok, response, strlen(response) + 1);
    jerr = json_tokener_get_error(tok);
    if (jerr != json_tokener_success)
    {
        fprintf(stderr, "Error: %s\n", json_tokener_error_desc(jerr));
        return -1;
    }

    json_object_object_get_ex(rootObj, "access_token", &tokenStrObj);
    if (json_object_get_type(tokenStrObj) != json_type_string)
    {
        return -2;
    }
    tokenstr = (char *)json_object_get_string(tokenStrObj);
    tokLen = (int) strlen(tokenstr);
    *token = malloc(tokLen + 1);
    memset(*token, 0, tokLen + 1);
    memcpy(*token, tokenstr, tokLen);
    return 0;
}

/*****************************************************************/
/* Build the curl command to access the Token from the AuthToken */
/* server.                                                       */
/* DISCLAIMER: CURL interaction code source from                 */
/*             https://curl.se/libcurl/c/getinmemory.html        */
/*****************************************************************/
static int AuthTokenLogin(char *TokenEndpoint, char *UserId, char *Password, char *ClientId, char **response)
{
    int rc = 0;
    CURL *curl;
    CURLcode res;
    char POSTOPTBUILDUP[65536] = {0};
    struct MemoryStruct chunk;

    chunk.memory = malloc(1);
    chunk.size = 0;

    if (debugPrint)
    {
      printf("> Global curl init\n");
    }
    curl_global_init(CURL_GLOBAL_ALL);
    /* Obtain token */
    if (debugPrint)
    {
      printf("> curl init\n");
    }
    curl = curl_easy_init();
    if (curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, TokenEndpoint);
        sprintf(POSTOPTBUILDUP, "username=%s&password=%s&grant_type=password&client_id=%s", UserId, Password, ClientId);
        /* WARNING: We are disabling Peer certificate and Host verification here for simplicity. */
        /*          Do not do this in production!                                                */
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, POSTOPTBUILDUP);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        if (debugPrint)
        {
          printf("> connecting!\n");
        }
        
        res = curl_easy_perform(curl);
        if (res != CURLE_OK)
        {
            fprintf(stderr, "curl_easy_perform() failed: %s\n",
                    curl_easy_strerror(res));
            rc = res;
        }
        else
        {
            *response = malloc(chunk.size + 1);
            memset(*response, 0, chunk.size + 1);
            memcpy(*response, chunk.memory, chunk.size);
        }
    }
    else
    {
        /* Failed to */
        printf("FAILED TO INIT CURL!\n");
        rc = -2;
    }

    memset(chunk.memory, 0, chunk.size);
    free(chunk.memory);

    curl_easy_cleanup(curl);
    /* code */
    return rc;
}

/*********************************************************************/
/*                                                                   */
/* Function Name:  ChlExit                                           */
/*                                                                   */
/* Description: Main channel exit function. Use defined environment  */
/*              variables to obtain a token from a AuthToken server  */
/*              and use it for authenticating with the Queue Manager */
/*                                                                   */
/*********************************************************************/
void MQENTRY ChlExit(
  PMQVOID  pChannelExitParms,   /* Channel exit parameter block */
  PMQVOID  pChannelDefinition,  /* Channel definition           */
  PMQLONG  pDataLength,         /* Length of data               */
  PMQLONG  pAgentBufferLength,  /* Length of agent buffer       */
  PMQVOID  pAgentBuffer,        /* Agent buffer                 */
  PMQLONG  pExitBufferLength,   /* Length of exit buffer        */
  PMQPTR   pExitBufferAddr)     /* Address of exit buffer       */
{
  PMQCXP pParms = (PMQCXP)pChannelExitParms;
  PMQCD  pCD    = (PMQCD)pChannelDefinition;
  char scyData[MQ_EXIT_DATA_LENGTH+1] = {0};

  /* Define the JWT Environment variables to retrieve Token */
  /* from the AuthToken issuer.                              */
  char* jwtTokenEndpoint = getenv("JWT_TOKEN_ENDPOINT");
  char* jwtTokenUsername = getenv("JWT_TOKEN_USERNAME");
  char* jwtTokenPassword = getenv("JWT_TOKEN_PWD");
  char* jwtTokenClientId = getenv("JWT_TOKEN_CLIENTID");
  char* token = NULL_POINTER;

  if (pParms->ExitId == MQXT_CHANNEL_SEC_EXIT)
  {
    /*****************************************************************/
    /* The MQXR_SEC_PARMS invocation for security exit is done to    */
    /* permit the CSP structure to be filled in and passed to the    */
    /* server. If there is already a CSP structure, then this exit   */
    /* will not override it.                                         */
    /*****************************************************************/
    if (pParms->ExitReason == MQXR_SEC_PARMS)
    {
      strncpy(scyData,pParms->ExitData,MQ_EXIT_DATA_LENGTH);
      scyData[MQ_EXIT_DATA_LENGTH] = 0;
      if (strstr(scyData,DEBUG_OPTION)) { /* This is safe because scyData is known to be NULL-terminated */
        debugPrint = TRUE;
      }  
      if (obtainToken(jwtTokenEndpoint, jwtTokenUsername, jwtTokenPassword, jwtTokenClientId, &token) != 0)
      {
        pParms->ExitResponse = MQXCC_CLOSE_CHANNEL;
      }
      else
      {
        MQCSP csp = {MQCSP_DEFAULT};
        if (debugPrint)
        {
          printf("Token to be used:\n%s\n", token);
        }
        pParms->SecurityParms = malloc(sizeof(MQCSP) + strlen(token));
        if (pParms->SecurityParms != NULL)
        {
          memcpy(pParms->SecurityParms,&csp,sizeof(MQCSP));
          pParms->SecurityParms->Version = 3;
          pParms->SecurityParms->AuthenticationType =  MQCSP_AUTH_ID_TOKEN;

          pParms->SecurityParms->TokenLength = (MQLONG)strlen(token);
          pParms->SecurityParms->TokenOffset = sizeof(MQCSP);
          memcpy((char *)pParms->SecurityParms + sizeof(MQCSP),token,strlen(token));

          pParms->ExitUserArea[0] = SUCCESS;
        }
      }
    }
    else if (pParms->ExitReason == MQXR_TERM)
    {
      if (pParms->Version > 5)
      {
        if (pParms->SecurityParms != NULL &&
            pParms->ExitUserArea[0] == SUCCESS)
          free(pParms->SecurityParms);
      }
    }
  }
  return;
}

/*********************************************************************/
/*                                                                   */
/* Function Name:  MQStart                                           */
/*                                                                   */
/* Description: Entrypoint to the exit                               */
/*********************************************************************/
void MQENTRY MQStart ( PMQCXP  pChannelExitParms
                     , PMQCD   pChannelDefinition
                     , PMQLONG pDataLength
                     , PMQLONG pAgentBufferLength
                     , PMQVOID pAgentBuffer
                     , PMQLONG pExitBufferLength
                     , PMQPTR  pExitBufferAddr
                     )
{
  ChlExit ( pChannelExitParms
          , pChannelDefinition
          , pDataLength
          , pAgentBufferLength
          , pAgentBuffer
          , pExitBufferLength
          , pExitBufferAddr
          );
}
