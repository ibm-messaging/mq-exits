/*****************************************************************************/
/*                                                                           */
/* Module Name: amqsxlbl.c                                                   */
/*                                                                           */
/* (C) IBM Corporation 2022                                                  */
/*                                                                           */
/* AUTHOR: Chris Leonard, IBM Hursley                                        */
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
/* Description: A sample channel auto definitions exit for IBM MQ that       */
/*              provides customised mappings of the certificate label field  */
/*              for automatically defined cluster sender channels.           */
/*                                                                           */
/*****************************************************************************/
 
/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
/* Common includes */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
 
/* IBM MQ includes */
#include <cmqc.h>
#include <cmqxc.h>           
 
/******************************************************************************/
/* Defines                                                                    */
/******************************************************************************/
/* Size of buffer to use when reading each line from config file */
#define CONFIG_BUFFER_SIZE 2048

#if (MQPL_NATIVE == MQPL_WINDOWS_NT)
  #define DLLEXPORT _declspec(dllexport)
#else
  #define DLLEXPORT 
#endif

/* The object name will be a cluster name or channel name */
/* MQ_CLUSTER_NAME_LENGTH is larger than MQ_CHANNEL_NAME_LENGTH */
#define OBJECT_NAME_LENGTH MQ_CLUSTER_NAME_LENGTH


/* The prefixes that will be used when parsing the config file */
#define CONFIG_PREFIX_CHANNEL "channel."
#define CONFIG_PREFIX_CLUSTER "cluster."


/* Standard constants */
#ifndef FALSE
  #define FALSE (0)
#endif
#ifndef TRUE
  #define TRUE (1)
#endif
#define OK (0)

#define logitIfEnabled(pLogFile, format, ...) if (pLogFile != NULL) logit(pLogFile, format, ##__VA_ARGS__)

/* Structure linked list elements holding parsed config data. */
typedef struct xlblMapEntry XLBLMAPENTRY, *PXLBLMAPENTRY;
struct xlblMapEntry
{
  MQBYTE   entryType;                              /* MAP_ENTRY_TYPE_CHANNEL or MAP_ENTRY_TYPE_CLUSTER          */
  MQCHAR   objectName[OBJECT_NAME_LENGTH];         /* Channel or cluster name, dependent on entryType           */
  MQCHAR   certificateLabel[MQ_CERT_LABEL_LENGTH]; /* CERTLABL to set if this record matches the active channel */
  XLBLMAPENTRY * pNextEntry;                       /* Pointer to the next entry in the list                     */
};
/* entryType constants */
#define MAP_ENTRY_TYPE_CHANNEL 1
#define MAP_ENTRY_TYPE_CLUSTER 2

/* Config data - pointer to head of mapping list */
typedef struct xlblData
{
  XLBLMAPENTRY * pMapList;
} XLBLDATA, *PXLBLDATA;
 
/* Prototypes */ 
int readConfigFile(PXLBLDATA pData, FILE * pLogFile);
void freeMapList(PXLBLDATA pData);
MQBOOL evalMapList(PXLBLDATA pData, PMQCD pChannelDefinition, FILE * pLogFile);
static char *trim(char *line);
void logit(FILE * pLogFile, const char * fmt, ...);


/*********************************************************************/
/*                                                                   */
/* Function Name:  ChlExit                                           */
/*                                                                   */
/* Description: Main entry point for this channel auto definition    */
/*              exit. Expects to be called for:                      */
/*                MQXR_INIT                                          */
/*                MQXR_AUTO_CLUSSDR                                  */
/*                MQXR_TERM                                          */
/*                                                                   */
/*              Takes no action if called for:                       */
/*                MQXR_AUTO_CLUSRCVR                                 */
/*                MQXR_AUTO_RECEIVER                                 */
/*                MQXR_AUTO_SVRCONN:                                 */
/*                                                                   */
/*              Invocations for any other exit reason will result    */
/*              in an error.                                         */
/*              record matches the current channel.                  */
/*                                                                   */
/*              No validity checking is performed on the             */
/*              channelName, clusterName or certlabl, other than     */
/*              ensuring its length is within the permitted range.   */
/*                                                                   */
/*              When called for MQXR_INIT, reads the file specified  */
/*              by the MQXLBL_CONFIG_FILE environment variable, to   */
/*              establish the list of channel/cluster -> certlabl    */
/*              mappings to apply.                                   */
/*                                                                   */
/*              When called for MQXR_AUTO_CLUSSDR, evaluates the     */
/*              the channel name and cluster name(s) of the current  */
/*              channel against this list, altering the channel's    */
/*              CERTLABL attribute based on the first match found.   */
/*                                                                   */
/*              A second environment variable MQXLBL_LOG_FILE may be */
/*              optionally set to specify a file to which to         */
/*              generate log output for diagnostic purposes.         */
/*********************************************************************/
DLLEXPORT void MQENTRY ChlExit(PMQCXP pChannelExitParms, PMQCD pChannelDefinition)
{
  FILE      * pLogFile       = NULL;
  MQLONG      rc             = 1;
  char      * env            = NULL;
  PXLBLDATA   pData = (PXLBLDATA)&pChannelExitParms->ExitUserArea;
  
  env = getenv( "MQXLBL_LOG_FILE" );
  if (env)
  {
    pLogFile = fopen(env,"a");
    /* Errors ignored - pLogFile==NULL suppresses logging */
  }
  
  /***************************************************************************/
  /* Switch on the reason the exit was called                                */
  /***************************************************************************/
  switch (pChannelExitParms->ExitReason)
  {
    case MQXR_INIT:
      
      logitIfEnabled(pLogFile, "Called for MQXR_INIT\n");
      
      rc = readConfigFile(pData, pLogFile);
      
      if (rc)
      {
        pChannelExitParms->Feedback = rc;
        goto mod_exit;
      }        

      break;
    case MQXR_AUTO_CLUSRCVR:
    case MQXR_AUTO_RECEIVER:
    case MQXR_AUTO_SVRCONN:
      /* Return OK, as there's nothing to do here for other channel types, and we still want the channel to start*/
      rc = OK;
      break;
    
    case MQXR_AUTO_CLUSSDR:
      logitIfEnabled(pLogFile, "Called for MQXR_AUTO_CLUSSDR for channel %.*s\n",
                     MQ_CHANNEL_NAME_LENGTH, pChannelDefinition->ChannelName);
      
      /* Certlabl requires at least version 11 of the MQCD */
      if (pChannelDefinition->Version < MQCD_VERSION_11)
      {
        logitIfEnabled(pLogFile, "Supplied MQCD version '%d' does not contain certlabl information. Unable to proceed\n",pChannelDefinition->Version);
        break;
      }
      
      if(!evalMapList(pData, pChannelDefinition, pLogFile))
      {
        logitIfEnabled(pLogFile, "No match found\n");
      }

      rc = OK;
      break;
      
    case MQXR_TERM:

      logitIfEnabled(pLogFile, "Called for MQXR_TERM. Freeing resources\n");
      freeMapList(pData);
      pData->pMapList = NULL;
      rc = OK;
      break;
      
    default:
      logitIfEnabled(pLogFile, "Called for unexpected RC: %d.\n", pChannelExitParms->ExitReason );


  } /* endswitch */

mod_exit:

  if (pLogFile)
  {
    fclose(pLogFile);
  }
  
  if (rc)
  {
    pChannelExitParms->ExitResponse = MQXCC_SUPPRESS_FUNCTION;
  }
  else
  {
    pChannelExitParms->ExitResponse = MQXCC_OK;
  }
 
}

/*********************************************************************/
/*                                                                   */
/* Function Name:  readConfigFile                                    */
/*                                                                   */
/* Description: Reads the configuration file specified by the        */
/*              MQXLBL_CONFIG_FILE environment variable.             */
/*              Lines are trimmed of whitespace, and any lines       */
/*              prefixed  with '#' ignored as a comment.             */
/*                                                                   */
/*              The following formats are valid:                     */
/*                  channel.channelName=certlabl                     */
/*                  cluster.clusterlName=certlabl                    */
/*                                                                   */
/*              Where channelName and clusterName are replaced by    */
/*              the object name to be evaluated, and certlabl is     */
/*              the value that will be added to the MQCD if the      */
/*              record matches the current channel.                  */
/*                                                                   */
/*              No validity checking is performed on the             */
/*              channelName, clusterName or certlabl, other than     */
/*              ensuring its length is within the permitted range.   */
/*********************************************************************/
int readConfigFile(XLBLDATA * pData, FILE * pLogFile)
{
  FILE          * pConfigFile = NULL;
  MQLONG          rc = 0;
  char            line[CONFIG_BUFFER_SIZE] = {0};
  char          * p = NULL;
  char          * env = NULL;
  char          * equalsPos = NULL;
  char          * objectNamePos = NULL;
  MQLONG          lineNumber = 0;
  size_t          entryObjectNameLength = 0;
  size_t          certLabelLength;
  PXLBLMAPENTRY   pCurrentMapEntry = NULL;
  PXLBLMAPENTRY   pNewMapEntry = NULL;
  
  env = getenv( "MQXLBL_CONFIG_FILE" );
  if (env)
  {
    logitIfEnabled(pLogFile, "Opening config file: %s\n", env);
    
    pConfigFile = fopen(env, "r");
    if (!pConfigFile)
    
    {
      logitIfEnabled(pLogFile, "Error opening config file: %d\n", errno);
      rc = MQRC_FILE_SYSTEM_ERROR;
      goto MOD_EXIT;
    }
    
  }
  else
  {
    logitIfEnabled(pLogFile, "MQXLBL_CONFIG_FILE env var must be set");
    rc = MQRC_ENVIRONMENT_ERROR;
    goto MOD_EXIT;
  }
  
  
  while (fgets(line,sizeof(line)-1,pConfigFile) != NULL)
  {
    MQBYTE entryType;
    lineNumber++;
    p = trim(line);
    
    /* Ignore comment lines prefixed with '#' and blank lines */
    if ( p[0] != '#' && strlen(p) > 0 )
    {
      if( strncmp(CONFIG_PREFIX_CHANNEL, p, sizeof(CONFIG_PREFIX_CHANNEL) - 1 ) == 0 )
      {
        entryType = MAP_ENTRY_TYPE_CHANNEL;
        objectNamePos = p + sizeof(CONFIG_PREFIX_CHANNEL) - 1;
      }
      else if ( strncmp(CONFIG_PREFIX_CLUSTER, p, sizeof(CONFIG_PREFIX_CLUSTER) - 1 ) == 0 )
      {
        entryType = MAP_ENTRY_TYPE_CLUSTER;
        objectNamePos = p + sizeof(CONFIG_PREFIX_CLUSTER) - 1;
      }
      else
      {
        logitIfEnabled(pLogFile, "Unexpected value on line %d, ignoring.\n", lineNumber);
        continue;
      }
      
      equalsPos = strchr(p,'=');
      
      if (!equalsPos)
      {
        logitIfEnabled(pLogFile, "Unexpected value on line %d, ignoring.\n", lineNumber);
        continue;
      }

      /* The object name is the part after the prefix, before the '=' */
      /* Check that it's of suitable length */
      entryObjectNameLength = equalsPos - objectNamePos;
      if (entryType == MAP_ENTRY_TYPE_CHANNEL)
      {
        
        if (entryObjectNameLength < 1 || entryObjectNameLength > MQ_CHANNEL_NAME_LENGTH)
        {
          logitIfEnabled(pLogFile, "Channel name length error on line %d, ignoring.\n", lineNumber);
          continue;
        }
      }
      else
      {
        if (entryObjectNameLength < 1 || entryObjectNameLength > MQ_CLUSTER_NAME_LENGTH)
        {
          logitIfEnabled(pLogFile, "Cluster name length error on line %d, ignoring.\n", lineNumber);
          continue;
        }
      }
      
      certLabelLength = strlen(equalsPos+1);
      if (certLabelLength < 1 || certLabelLength > MQ_CERT_LABEL_LENGTH)
      {
        logitIfEnabled(pLogFile, "Certificate label length error on line %d, ignoring.\n", lineNumber);
        continue;
      }
      
      /* Looks like we've got a valid config entry we can work with - allocate a new entry for the mapping list */
      pNewMapEntry = malloc(sizeof(XLBLMAPENTRY));
      pNewMapEntry->entryType = entryType;
      pNewMapEntry->pNextEntry = NULL;
      memset(pNewMapEntry->objectName, ' ', OBJECT_NAME_LENGTH);
      memcpy(pNewMapEntry->objectName, objectNamePos, entryObjectNameLength);
      memset(pNewMapEntry->certificateLabel, 0, MQ_CERT_LABEL_LENGTH);
      memcpy(pNewMapEntry->certificateLabel, equalsPos+1, certLabelLength);
      
      /* If this is the first entry, remember it as the head pointer */
      if (pData->pMapList == NULL)
      {
        pData->pMapList = pNewMapEntry;
      }
      /* otherwise add this entry to the end of the list */
      else
      {
        pCurrentMapEntry->pNextEntry = pNewMapEntry;
      }
      
      /* Remember the end of the list */
      pCurrentMapEntry = pNewMapEntry;
      
      logitIfEnabled(pLogFile, "Added new mapping (type %d) for '%.*s' to certlabl '%.*s'\n", 
                     (int)pNewMapEntry->entryType,
                     OBJECT_NAME_LENGTH, pNewMapEntry->objectName, 
                     MQ_CERT_LABEL_LENGTH, pNewMapEntry->certificateLabel);
    }

    
  }
  
MOD_EXIT:

  if (pConfigFile)
  {
    fclose(pConfigFile);
  }
  
  return rc;
}

/*********************************************************************/
/*                                                                   */
/* Function Name:  evalMapList                                       */
/*                                                                   */
/* Description: Iterate over the list of mappings read from the      */
/*              config file, to determine if a match is found for    */
/*              the current channel definition. The channel name     */
/*              and any cluster names are considered. The first      */
/*              match found (based on order of the config file) will */
/*              be applied, and the corresponding certificate label  */
/*              copied into the supplied channel definition.         */
/*********************************************************************/
MQBOOL evalMapList(PXLBLDATA pData, PMQCD pChannelDefinition, FILE * pLogFile)
{
  PXLBLMAPENTRY pCurrentMapEntry = pData->pMapList; /* Current position in the linked list to evaluate */
  MQBOOL bFound = FALSE;
  
  /* Loop over the mappings - rules are evaluated in the same order as the file - first match wins */
  while (pCurrentMapEntry != NULL)
  {
    if (pCurrentMapEntry->entryType == MAP_ENTRY_TYPE_CHANNEL)
    {
      logitIfEnabled(pLogFile, "Comparing channel name '%.*s' with '%.*s'\n",
                     MQ_CHANNEL_NAME_LENGTH, pCurrentMapEntry->objectName,
                     MQ_CHANNEL_NAME_LENGTH, pChannelDefinition->ChannelName );
      if(memcmp(pCurrentMapEntry->objectName, pChannelDefinition->ChannelName, MQ_CHANNEL_NAME_LENGTH) == 0)
      {

        logitIfEnabled(pLogFile, "Matched channel name '%.*s', updating MQCD with CERTLABL '%.*s'\n", MQ_CHANNEL_NAME_LENGTH, pCurrentMapEntry->objectName, MQ_CERT_LABEL_LENGTH, pCurrentMapEntry->certificateLabel );
        bFound = TRUE;
        goto MOD_EXIT;
      }
      else
      {
        logitIfEnabled(pLogFile, "No match\n");
      }
    }
    else if (pCurrentMapEntry->entryType == MAP_ENTRY_TYPE_CLUSTER)
    {
      int i;
      char * pClusName = pChannelDefinition->ClusterPtr;
      
      /* There could be more than one cluster name in the MQCD, so we'll loop over them.*/
      for (i=0; i < pChannelDefinition->ClustersDefined; i++)
      {
        logitIfEnabled(pLogFile, "Comparing cluster name '%.*s' with '%.*s'\n",
                       MQ_CLUSTER_NAME_LENGTH, pCurrentMapEntry->objectName,
                       MQ_CLUSTER_NAME_LENGTH, pClusName );
        if(memcmp(pCurrentMapEntry->objectName, pClusName, MQ_CLUSTER_NAME_LENGTH) == 0)
        {
          logitIfEnabled(pLogFile, "Matched cluster name '%.*s', updating MQCD with CERTLABL '%.*s'\n", MQ_CLUSTER_NAME_LENGTH, pCurrentMapEntry->objectName, MQ_CERT_LABEL_LENGTH, pCurrentMapEntry->certificateLabel);
          bFound = TRUE;
          goto MOD_EXIT;
        }
        else
        {
          logitIfEnabled(pLogFile, "No match\n");
        }
        /* Go around again for the next cluster name */
        pClusName = pClusName + MQ_CLUSTER_NAME_LENGTH;
      }
    }
    else
    {
      logitIfEnabled(pLogFile, "Could not understand entry type %d\n", pCurrentMapEntry->entryType);
    }
    /* Evaluate the next element in the list */ 
    pCurrentMapEntry = pCurrentMapEntry->pNextEntry;
  }
  
MOD_EXIT:

  if ( bFound )
  {
    memcpy(pChannelDefinition->CertificateLabel, pCurrentMapEntry->certificateLabel, MQ_CERT_LABEL_LENGTH);
  }
  
  return bFound;
}

/*********************************************************************/
/*                                                                   */
/* Function Name:  FreeMapList                                       */
/*                                                                   */
/* Description: Free the list of certlabl mappings that were         */
/*              previously read from the config file.                */
/*********************************************************************/
void freeMapList(PXLBLDATA pData)
{
  XLBLMAPENTRY * pCurrentMapEntry = NULL;
  
  /* Double check we haven't been passed a null pointer */
  if ( pData != NULL ) 
  {
    /* Free each entry in the map list, updating the */
    /* head pointer to point to the next element     */
    while(pData->pMapList != NULL)
    {
      pCurrentMapEntry = pData->pMapList;
      pData->pMapList = pData->pMapList->pNextEntry;
      
      free(pCurrentMapEntry);
    }
  }
}

/*********************************************************************/
/*                                                                   */
/* Function Name:  trim                                              */
/*                                                                   */
/* Description: Remove leading and trailing whitespace from a string */
/*              Return a pointer to the new start of the string.     */
/*********************************************************************/
static char *trim(char *line)
{
  char *p;

  /* Remove trailing spaces */
  p=&line[strlen(line)-1];
  while (p>=line)
  {
    if (isspace((unsigned char)*p))
      *p=0;
    else
      break;
    p--;
  }

  /* Remove leading spaces */
  p=line;
  while (*p != 0 && isspace((unsigned char)*p))
  {
    p++;
  }
  return p;
}

/*********************************************************************/
/*                                                                   */
/* Function Name:  logit                                             */
/*                                                                   */
/* Description: Writes a timestamp followed by the supplied log      */
/*              message to the supplied file handle.                 */
/*********************************************************************/
void logit(FILE * pLogFile, const char * fmt, ...)
{
  time_t   t = time(NULL);
  struct tm * now;
  va_list  arg_ptr;
  int      n;
  
  if (pLogFile != NULL)
  {
    va_start(arg_ptr, fmt);
    now = localtime(&t);
    fprintf(pLogFile , "%04d%02d%02d %02d:%02d:%02d ",
                       now->tm_year+1900,
                       now->tm_mon+1,
                       now->tm_mday,
                       now->tm_hour,
                       now->tm_min,
                       now->tm_sec );
    vfprintf(pLogFile, fmt, arg_ptr);
    va_end(arg_ptr);
  }
  return;
}

/*********************************************************************/
/*                                                                   */
/* Standard MQ Entrypoint (unused)                                   */
/*                                                                   */
/*********************************************************************/

void MQStart()
{
  ;
}