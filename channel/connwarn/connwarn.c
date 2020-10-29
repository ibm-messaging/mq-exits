/*****************************************************************************/
/*                                                                           */
/* Module Name: connwarn.c                                                   */
/*                                                                           */
/* (C) IBM Corporation 2020                                                  */
/*                                                                           */
/* AUTHOR: Rob Parker, IBM Hursley                                           */
/*                                                                           */
/* PLEASE NOTE - This code is supplied "AS IS" with no                       */
/*              warranty or liability. It is not part of                     */
/*              any product.                                                 */
/*                                                                           */
/* Description: A sample Security exit for IBM MQ that provides Connection   */
/*              Authentication warn mode functionality.                      */
/*                                                                           */
/*****************************************************************************/

/* IBM MQ includes */
#include <cmqc.h>             /* MQI header                          */
#include <cmqxc.h>            /* Installable services header         */

/* Common includes */
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* OS specific includes */
#ifdef OS_WINDOWS
    #define MQ_FNAME_MAX _MAX_FNAME
    #define MQ_PATH_MAX _MAX_PATH
    #include <windows.h>
    #include <winbase.h>
    #define DllExport   __declspec( dllexport )
#else
    #include <limits.h>
    #include <sys/file.h>
    #define MQ_FNAME_MAX NAME_MAX
    #define MQ_PATH_MAX PATH_MAX
    #define DllExport
#endif

/* Prototypes */
MQBOOL validateCredentials(PMQCSP pCSP);
void chlname_to_filename(char * chlname, char * filename, int maxsize);
void trim_whitespace(char * toTrim);

/* Constants */
#define FALSE 0
#define TRUE  1

#ifdef OS_WINDOWS
    #define DEFAULT_LOG_LOCATION "C:\\ProgramData\\IBM\\MQ\\errors"
#else
    #define DEFAULT_LOG_LOCATION "/var/mqm/errors/"
#endif

/* 
    Main channel exit function. 
    Verifies contents of the MQCSP and MQCD userid/password contents and 
    outputs whether they exist and are valid.
 */
DllExport void MQENTRY ChlExit (PMQVOID pChannelExitParms,
                        PMQVOID pChannelDefinition,
                        PMQLONG pDataLength,
                        PMQLONG pAgentBufferLength,
                        PMQVOID pAgentBuffer,
                        PMQLONG pExitBufferLength,
                        PMQPTR  pExitBufferAddr)
{
    PMQCXP pParms = (PMQCXP)pChannelExitParms;
    PMQCD  pCD    = (PMQCD)pChannelDefinition;
    PMQCSP pCSP;

    MQBOOL mqcdCredentials    = FALSE;
    MQBOOL mqcspCredentials   = FALSE;
    MQBOOL mqcspCredsValid    = FALSE;
    MQBOOL mqcspismqcd        = FALSE;
    MQBOOL defaultLogLocation = TRUE;
    
    FILE * File;
    char filename[MQ_FNAME_MAX + 1]                 = {0};
    char path[MQ_PATH_MAX + 1]                      = {0};
    char connectionName[MQ_CONN_NAME_LENGTH + 1]    = {0}; 
    char remoteUser[MQ_Q_MGR_NAME_LENGTH + 1]       = {0}; /* 49 as we can pull from PartnerName which is a max of 48 + 1 null character */
    char mqcdUser[MQ_USER_ID_LENGTH + 1]            = {0};

    /* Only print out when we have a security exit being called because of MQXR_SEC_PARMS */
    if(pParms->ExitId == MQXT_CHANNEL_SEC_EXIT && pParms->ExitReason == MQXR_SEC_PARMS)
    {
        /* Convert the channel name into a filename */
        chlname_to_filename(pCD->ChannelName, &filename[0], FILENAME_MAX);

        if(pParms->Version >= MQCXP_VERSION_2 && 
        (pParms->ExitData[0] != ' ' || pParms->ExitData[0] != '\0'))
        {
            defaultLogLocation = FALSE;
            /* Exit Data has been supplied which should be a file path */
            strncpy(path,pParms->ExitData, MQ_PATH_MAX);
            trim_whitespace(&path[0]);
            /* Check we have a trailing slash */
#ifdef OS_WINDOWS
            if(path[strlen(path) -1] != '\\')
            {
                strcat(path, "\\");
            }
#else
            if(path[strlen(path) -1] != '/')
            {
                strcat(path, "/");
            }
#endif
        }
        else
        {
            /* Use default location */
            strcpy(path, DEFAULT_LOG_LOCATION);
            defaultLogLocation = TRUE;
        }
        /* Add the filename we determined from the channel name to the path for later */
        strncat(path, filename, MQ_PATH_MAX - strlen(path));

        /* Check if we have a MQCD userid/password. We cannot only check pCD->RemoteUserIdentifier */
        /* as this will get set automatically during MQ processing.                                */
        if(pCD->Version >= MQCD_VERSION_2 && 
        (pCD->RemotePassword[0] != ' ' && pCD->RemotePassword[0] != '\0'))
        {
            mqcdCredentials = TRUE;
            strncpy(mqcdUser, pCD->RemoteUserIdentifier, MQ_USER_ID_LENGTH);
        }
        else
        {
            strcpy(mqcdUser, "N/A");
        }

        /* Check if we have been passed a MQCSP */
        /* Note: If we have been passed MQCD credentials then this will either */
        /* be unique credentials or a copy of the credentials held in the MQCD */
        if(pParms->Version >= MQCXP_VERSION_6 && pParms->SecurityParms)
        {
            pCSP = (PMQCSP) pParms->SecurityParms;
            if(pCSP->CSPUserIdLength > 0 || pCSP->CSPPasswordLength > 0)
            {
                mqcspCredentials = TRUE;
                mqcspCredsValid = validateCredentials(pCSP);
            }
        }
        
        /* If we have both MQCD and MQCSP credentials we should check if they are the same */
        if(mqcdCredentials && mqcspCredentials)
        {
            char cdUser[MQ_USER_ID_LENGTH + 1]  = {0};
            char cdPass[MQ_PASSWORD_LENGTH + 1] = {0};
            strncpy(cdUser, pCD->RemoteUserIdentifier, MQ_USER_ID_LENGTH);
            strncpy(cdPass, pCD->RemotePassword, MQ_PASSWORD_LENGTH);
            trim_whitespace(cdUser);
            trim_whitespace(cdPass);

            if((strlen(cdUser) == pCSP->CSPUserIdLength) && 
               (strlen(cdPass) == pCSP->CSPPasswordLength))
            {
                /* The entry in the pCSP is not necessarily null terminated */
                if((strncmp(cdUser, pCSP->CSPUserIdPtr, pCSP->CSPUserIdLength) == 0) &&
                   (strncmp(cdPass, pCSP->CSPPasswordPtr, pCSP->CSPPasswordLength) == 0))
                {
                    mqcspismqcd = TRUE;
                }
            }
        }

        /* Gather details for printing log entry */
        if(pCD->Version >= MQCD_VERSION_2)
        {
            strncpy(connectionName, pCD->ConnectionName, MQ_CONN_NAME_LENGTH);
            strncpy(remoteUser, pCD->RemoteUserIdentifier, MQ_USER_ID_LENGTH);
        }
        else
        {
            strncpy(connectionName, pCD->ShortConnectionName, MQ_SHORT_CONN_NAME_LENGTH);
            if(pParms->Version >= MQCXP_VERSION_3)
            {
                strncpy(remoteUser, pParms->PartnerName, MQ_Q_MGR_NAME_LENGTH);
            }
            else
            {
                strcpy(remoteUser, "[UNKNOWN]");
            }
        }

        /* We copy and trim the various fields because we should not edit them in the MQCD/MQCXP */
        trim_whitespace(connectionName);
        trim_whitespace(remoteUser);
        if(mqcdCredentials)
        {
            trim_whitespace(mqcdUser);
        }

WRITE_LOG:
        /* Now open and lock the log file */
        File = fopen(path, "a");
        if(File)
        {
            /* Generate timestamp in RFC 3339 format */
            time_t rawtime;
            struct tm * timeinfo;
            char back[40] = {0};
            char * timestamp = &back[0];
            time ( &rawtime );
            timeinfo = localtime ( &rawtime );
            strftime (timestamp, 40, "%FT%T%z",timeinfo);
   
#ifdef OS_UNIX
            /* Lock the file on UNIX */
            flock(fileno(File), LOCK_EX);
#endif
            /* Write the log output */
            fprintf(File, "%s\nConnection from %s running as %s\n", timestamp, connectionName, remoteUser);
            fprintf(File, "           MQCD Credentials Supplied: %s (%s)\n", (mqcdCredentials) ? "Y" : "N", mqcdUser);
            fprintf(File, "          MQCSP Credentials Supplied: %s (%.*s)\n", (mqcspCredentials) ? "Y" : "N", (mqcspCredentials) ? pCSP->CSPUserIdLength : 3, (mqcspCredentials) ? (char *)pCSP->CSPUserIdPtr : "N/A");
            fprintf(File, "MQCD and MQCSP Credentials Identical: %s\n", (mqcdCredentials) ? (mqcspismqcd) ? "Y (User Case insensitive)" : "N" : "N/A");
            fprintf(File, "             MQCSP Credentials Valid: %s\n", (mqcspCredentials) ? (mqcspCredsValid) ? "Y" : "N" : "N/A");
            fprintf(File, "---------------------------------------\n");

#ifdef OS_UNIX
            /* Remove our Lock on the file */
            flock(fileno(File), LOCK_UN);
#endif
            /* Close the file */
            fclose(File);
        }
        else
        {
            /* We failed to get the file handle. */
            /* Can we try and log to the default location as a backup? */
            if(defaultLogLocation == FALSE)
            {
                defaultLogLocation = TRUE;
                memset(path, '\0', strlen(path));
                strcpy(path, DEFAULT_LOG_LOCATION);
                strncat(path, filename, MQ_PATH_MAX - strlen(path));
                goto WRITE_LOG;
            }
            else 
            {
                /* If we get here we've already tried the default location */
                /* Fail with MQXCC_FAILED which will fail the connection   */
                /* with an error in the queue manager logs.                */
                pParms->ExitResponse = MQXCC_FAILED;
            }
        }
    }    
}

/* 
    Channel Exit standard function. Empty on purpose.
*/
void MQENTRY MQStart ( PMQCXP  pChannelExitParms
                     , PMQCD   pChannelDefinition
                     , PMQLONG pDataLength
                     , PMQLONG pAgentBufferLength
                     , PMQVOID pAgentBuffer
                     , PMQLONG pExitBufferLength
                     , PMQPTR  pExitBufferAddr
                     )
{;}

/* 
    Converts a channel name into a file name replacing non-alphanumeric characters 
*/
void chlname_to_filename(char * chlname, char * filename, int maxsize)
{
    char * pIn = chlname;
    char * pOut = filename;
    int currentSize = 0;

    /* Iterate through the channel name copying valid characters and replacing invalid filename characters */
    while(*pIn != ' ' && *pIn != '\0' && currentSize < MQ_CHANNEL_NAME_LENGTH)
    {
        switch(*pIn)
        {
            /* A character of '.' will be replaced with '2e-' */
            case '.':
                if((currentSize + 3) >= maxsize) /* Too big so stop now */
                    return;
                currentSize += 3;
                *pOut = '2';
                pOut++;
                *pOut = 'e';
                pOut++;
                *pOut = '-';
                pOut++;
                break;
            /* A character of '/' will be replaced with '2f-' */
            case '/':
                if((currentSize + 3) >= maxsize) /* Too big so stop now */
                    return;
                currentSize += 3;
                *pOut = '2';
                pOut++;
                *pOut = 'f';
                pOut++;
                *pOut = '-';
                pOut++;
                break;
            /* A character of '_' will be replaced with '5f-' */
            case '_':
                if((currentSize + 3) >= maxsize) /* Too big so stop now */
                    return;
                currentSize += 3;
                *pOut = '5';
                pOut++;
                *pOut = 'f';
                pOut++;
                *pOut = '-';
                pOut++;
                break;
            /* A character of '%' will be replaced with '25-' */
            case '%':
                if((currentSize + 3) >= maxsize) /* Too big so stop now */
                    return;
                currentSize += 3;
                *pOut = '2';
                pOut++;
                *pOut = '5';
                pOut++;
                *pOut = '-';
                pOut++;
                break;
            /* A character of '-' will be replaced with '2d-' */
            case '-':
                if((currentSize + 3) >= maxsize) /* Too big so stop now */
                    return;
                currentSize += 3;
                *pOut = '2';
                pOut++;
                *pOut = 'd';
                pOut++;
                *pOut = '-';
                pOut++;
                break;
            /* Any other characters are valid alphanumeric characters */
            default:
                currentSize++;
                *pOut = *pIn;
                pOut++;
                break;
        }

        if(currentSize >= maxsize)  /* Too big so stop now */
            return;

        pIn++;
    }
}

/* 
    Replaces the first whitespace after a non-whitespace character with a null character.
    This is done because MQ pads a lot of field with spaces 
*/
void trim_whitespace(char * toTrim)
{
    char * pEnd = toTrim + (strlen(toTrim) -1);
    while(isspace(*pEnd))
    {
        *pEnd = '\0';
        pEnd--;
    }    
}

/* 
Functions for validating the credentials. One for Windows one for Unix
*/
#ifdef OS_WINDOWS
MQBOOL validateCredentials(PMQCSP pCSP)
{
    HANDLE token;
    BOOL rc = FALSE;
    MQBOOL toReturn = FALSE;
    char * user;
    char * pass;

    /* First grab some memory to copy the userid and password into so we can add a null character at the end */
    user = malloc( sizeof(char) * (pCSP->CSPUserIdLength + 1));
    pass = malloc( sizeof(char) * (pCSP->CSPPasswordLength + 1));
    if(user != NULL && pass != NULL)
    {
        /* Copy userid and password into the malloc'd memory with a null character at the end */
        memset(user, '\0', pCSP->CSPUserIdLength + 1);
        memset(pass, '\0', pCSP->CSPPasswordLength + 1);
        strncpy(user, pCSP->CSPUserIdPtr, pCSP->CSPUserIdLength);
        strncpy(pass, pCSP->CSPPasswordPtr, pCSP->CSPPasswordLength);

        /* Run the windows logon with the userid and password to verify */
        rc = LogonUser( ( LPTSTR )user,
                    NULL,
                    ( LPTSTR )pass,
                    LOGON32_LOGON_NETWORK,
                    LOGON32_PROVIDER_DEFAULT,
                    &token );
        
        if(rc)
            toReturn = TRUE;

        if(token)
        {
            CloseHandle(token);
            token = NULL;
        }
    }

    /* Free any memory we allocated */
    if(user)
    {
        free(user);
    }
    if(pass)
    {
        free(pass);
    }

    return toReturn;
}
#else
MQBOOL validateCredentials(PMQCSP pCSP)
{
    MQBOOL rc = FALSE;
    char * commandBuffer;
    int buffLen = 0;
    int sizeOut = 0;
    int c;
    FILE * oampxstdout;

    /* Work out the command buffer length */
    buffLen += strlen("echo ");
    buffLen += pCSP->CSPPasswordLength;
    buffLen += strlen(" | ");
    buffLen += strlen("/opt/mqm/bin/security/amqoampx ");
    buffLen += pCSP->CSPUserIdLength;
    buffLen++; /* Null character */

    /* Now we have the buffer size we need grab some memory for it */
    commandBuffer = malloc(sizeof(char) * (buffLen));
    if(commandBuffer)
    {
        /* Write the command assuming default location of mq install directory */
        memset(commandBuffer, '\0', buffLen);
        strcat(commandBuffer, "echo ");
        strncat(commandBuffer, pCSP->CSPPasswordPtr, pCSP->CSPPasswordLength);
        strcat(commandBuffer, " | /opt/mqm/bin/security/amqoampx ");
        strncat(commandBuffer, pCSP->CSPUserIdPtr, pCSP->CSPUserIdLength);

        /* Call amqoampx and read the STDOUT */
        oampxstdout = popen(commandBuffer, "r");
        if (oampxstdout) {
            /* amqoampx will return '+0 [NULL]' when the userid/password is valid */
            if(getc(oampxstdout) == '+')
            {
                rc = TRUE;
            }
            pclose(oampxstdout);
        }
    }

    /* Free any memory we allocated */
    if(commandBuffer)
    {
        free(commandBuffer);
    }

    return rc;
}
#endif
