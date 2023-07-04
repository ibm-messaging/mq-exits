/*****************************************************************************/
/*                                                                           */
/* Module Name: connwarn.c                                                   */
/*                                                                           */
/* (C) IBM Corporation 2020                                                  */
/*                                                                           */
/* AUTHOR: Rob Parker, IBM Hursley                                           */
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
#if MQPL_NATIVE==MQPL_WINDOWS_NT
    #define MQ_FNAME_MAX _MAX_FNAME
    #define MQ_PATH_MAX _MAX_PATH
    #include <windows.h>
    #include <winbase.h>
    #define DllExport   __declspec( dllexport )
#elif MQPL_NATIVE==MQPL_UNIX
    #include <sys/wait.h>
    #include <signal.h>
    #include <limits.h>
    #include <sys/file.h>
    #include <unistd.h>
    #include <errno.h>
    #define MQ_FNAME_MAX FILENAME_MAX
    #define MQ_PATH_MAX PATH_MAX
    #define DllExport
#else
    #error "UNSUPPORTED PLATFORM"
#endif

/* Structure that contains values to be written to the log */
struct logValues {
    char * timestamp;
    char * conname;
    char * remoteAppUser;
    MQBOOL CDset;
    char * CDUser;
    MQBOOL CSPSet;
    int    CSPUserLen;
    char * CSPUser;
    MQBOOL identicalCDCSP;
    MQBOOL CSPValid;
};
#define DEFAULT_LOGVALUES "", "", "", FALSE, "", FALSE, 0, "", FALSE, FALSE

/* Prototypes */
int writeOutputEntry(char *, struct logValues);
int validateCredentials(PMQCSP pCSP);
void chlname_to_filename(char * chlname, char * filename, int maxsize);
void trim_whitespace(char * toTrim);

/* Constants */
#define OK    0
#define FAIL  -1
#define FALSE 0
#define TRUE  1

/* The max line a log entries line could be. As we may print */
/* a full MQCSP and the user in that is the largest          */
/* parameter we should base off that with some extra         */
#define MAX_LOG_LINE_LEN MQ_CLIENT_USER_ID_LENGTH + 50

#if MQPL_NATIVE==MQPL_WINDOWS_NT
    #define DEFAULT_LOG_LOCATION "C:\\ProgramData\\IBM\\MQ\\errors"
    #define DEFAULT_INSTALL_LOCATION "C:\\Program Files\\IBM\\MQ"
#elif MQPL_NATIVE==MQPL_UNIX
    #define DEFAULT_LOG_LOCATION "/var/mqm/errors/"
#if defined _AIX    
    #define DEFAULT_INSTALL_LOCATION "/usr/mqm"
#else
    #define DEFAULT_INSTALL_LOCATION "/opt/mqm"
#endif    
#else
    /* Not supported */
#endif

#if MQPL_NATIVE==MQPL_WINDOWS_NT
/* This marco is used to write out a line to a file on windows. */
#define WINDOWS_WRITE_ENTRY(str, parms)                                           \
{                                                                                 \
    snprintf(buffer, MAX_LOG_LINE_LEN, str, parms);                               \
    dwBytesToWrite = (DWORD)strlen(buffer);                                       \
    bErrorFlag = WriteFile(hFile, buffer, dwBytesToWrite, &dwBytesWritten, NULL); \
    if(bErrorFlag == FALSE || dwBytesWritten != dwBytesToWrite)                   \
    {                                                                             \
        return FAIL;                                                              \
    }                                                                             \
    memset(buffer, 0, MAX_LOG_LINE_LEN);                                          \
}
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

    MQBOOL defaultLogLocation = TRUE;
    int rc = OK;

    struct logValues outputValues                   = {DEFAULT_LOGVALUES};
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
#if MQPL_NATIVE==MQPL_WINDOWS_NT
            if(path[strlen(path) -1] != '\\')
            {
                strcat(path, "\\");
            }
#elif MQPL_NATIVE==MQPL_UNIX
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
            outputValues.CDset = TRUE;
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
                outputValues.CSPSet = TRUE;
                outputValues.CSPUser = (char *)pCSP->CSPUserIdPtr;
                outputValues.CSPUserLen = pCSP->CSPUserIdLength;

                int validaterc = validateCredentials(pCSP);
                if(validaterc == FALSE || validaterc == TRUE){ // Although the rc appears boolean, we may also have FAIL
                    outputValues.CSPValid = validaterc;
                }
                else
                {
                    /* If we get here the call to validate the credentials failed */
                    /* A likely cause is that amqoampx was not found */
                    pParms->ExitResponse = MQXCC_CLOSE_CHANNEL;
                    return;
                }
            }
        }
        
        /* If we have both MQCD and MQCSP credentials we should check if they are the same */
        if(outputValues.CDset && outputValues.CSPSet)
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
                    outputValues.identicalCDCSP = TRUE;
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

        if(outputValues.CDset)
        {
            trim_whitespace(mqcdUser);
        }

        /* Now write out the output! */
        outputValues.conname = connectionName;
        outputValues.remoteAppUser = remoteUser;
        outputValues.CDUser = mqcdUser;

        {
            /* Generate timestamp in RFC 3339 format */
            time_t rawtime;
            struct tm * timeinfo;
            char back[40] = {0};
            outputValues.timestamp = &back[0];
            time ( &rawtime );
            timeinfo = localtime ( &rawtime );
            strftime (outputValues.timestamp, 40, "%FT%T%z",timeinfo);

            rc = writeOutputEntry(path, outputValues);
            if(rc == FAIL)
            {
                /* We failed to get the file handle. */
                /* Can we try and log to the default location as a backup? */
                if(defaultLogLocation == FALSE)
                {
                    memset(path, '\0', strlen(path));
                    strcpy(path, DEFAULT_LOG_LOCATION);
                    strncat(path, filename, MQ_PATH_MAX - strlen(path));
                    rc = writeOutputEntry(path, outputValues);
                }

                if (rc == FAIL)
                {
                    /* If we get here we've already tried the default location */
                    /* or we failed when we tried to fall back to the default.  */
                    /* Fail with MQXCC_FAILED which will fail the connection   */
                    /* with an error in the queue manager logs.                */
                    pParms->ExitResponse = MQXCC_FAILED;
                }
            }
        }
    }    
}

/* 
    Channel Exit standard function. Empty on purpose.
*/
DllExport void MQENTRY MQStart ( PMQCXP  pChannelExitParms
                     , PMQCD   pChannelDefinition
                     , PMQLONG pDataLength
                     , PMQLONG pAgentBufferLength
                     , PMQVOID pAgentBuffer
                     , PMQLONG pExitBufferLength
                     , PMQPTR  pExitBufferAddr
                     )
{;}

/*
    Function for opening, locking and writing the log entry to a given file location
*/
int writeOutputEntry(char * path, struct logValues outputValues)
#if MQPL_NATIVE==MQPL_WINDOWS_NT
{
    HANDLE hFile;
    char buffer[MAX_LOG_LINE_LEN + 1] = {0};
    DWORD dwBytesToWrite, dwBytesWritten, dwPos;
    BOOL bErrorFlag = FALSE;

    // Open the file and lock it for this process
    hFile = CreateFileA(path, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if(hFile == INVALID_HANDLE_VALUE)
    {
        return FAIL;
    }

    dwPos = SetFilePointer(hFile, 0, NULL, FILE_END);

    // Write out the log entries
    WINDOWS_WRITE_ENTRY("-%s", "\n");
    WINDOWS_WRITE_ENTRY("  timestamp: \"%s\"\n", outputValues.timestamp);
    WINDOWS_WRITE_ENTRY("  remote_conname: \"%s\"\n", outputValues.conname);
    WINDOWS_WRITE_ENTRY("  remote_appuser: \"%s\"\n", outputValues.remoteAppUser);
    WINDOWS_WRITE_ENTRY("  MQCD_set: %s\n", (outputValues.CDset) ? "true" : "false");
    WINDOWS_WRITE_ENTRY("  MQCD_user: \"%s\"\n", (outputValues.CDset) ? outputValues.CDUser : "");
    WINDOWS_WRITE_ENTRY("  MQCSP_set: %s\n", (outputValues.CSPSet) ? "true" : "false");
    if(outputValues.CSPSet)
    {
        snprintf(buffer, MAX_LOG_LINE_LEN, "  MQCSP_user: \"%.*s\"\n", outputValues.CSPUserLen, outputValues.CSPUser);
        dwBytesToWrite = (DWORD)strlen(buffer);
        bErrorFlag = WriteFile(hFile, buffer, dwBytesToWrite, &dwBytesWritten, NULL);
        if(bErrorFlag == FALSE || dwBytesWritten != dwBytesToWrite)
        {
            UnlockFile(hAppend, dwPos, 0, dwBytesRead, 0);
            CloseHandle(hFile);
            return FAIL;
        }
        memset(buffer, 0, MAX_LOG_LINE_LEN);
    }
    else
    {
         WINDOWS_WRITE_ENTRY("  MQCSP_user: \"\"%s", "\n");
    }
    WINDOWS_WRITE_ENTRY("  MQCD_MQCSP_identical: %s\n", (outputValues.identicalCDCSP) ? "true" : "false");
    WINDOWS_WRITE_ENTRY("  MQCSP_valid: %s\n", (outputValues.CSPValid) ? "true" : "false");

    // Close the file handle
    CloseHandle(hFile);

    return OK;
}
#elif MQPL_NATIVE==MQPL_UNIX
{
    FILE * File;

    File = fopen(path, "a");
    if(File)
    {
        /* Lock the file on UNIX */
        flock(fileno(File), LOCK_EX);

        /* Write the log output as a YAML entry */
        fprintf(File, "-%s", "\n");
        fprintf(File, "  timestamp: \"%s\"\n", outputValues.timestamp);
        fprintf(File, "  remote_conname: \"%s\"\n", outputValues.conname);
        fprintf(File, "  remote_appuser: \"%s\"\n", outputValues.remoteAppUser);
        fprintf(File, "  MQCD_set: %s\n", (outputValues.CDset) ? "true" : "false");
        fprintf(File, "  MQCD_user: \"%s\"\n", (outputValues.CDset) ? outputValues.CDUser : "");
        fprintf(File, "  MQCSP_set: %s\n", (outputValues.CSPSet) ? "true" : "false");
        if(outputValues.CSPSet)
        {
            fprintf(File, "  MQCSP_user: \"%.*s\"\n", outputValues.CSPUserLen, outputValues.CSPUser);
        }
        else 
        {
            fprintf(File, "  MQCSP_user: \"\"%s", "\n");
        }
        fprintf(File, "  MQCD_MQCSP_identical: %s\n", (outputValues.identicalCDCSP) ? "true" : "false");
        fprintf(File, "  MQCSP_valid: %s\n", (outputValues.CSPValid) ? "true" : "false");
        /* End of YAML entry */

        /* Remove our Lock on the file */
        flock(fileno(File), LOCK_UN);
        /* Close the file */
        fclose(File);
    }
    else
    {
        return FAIL;
    }
    return OK;
}
#else
{
    return FAIL;
}
#endif

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
int validateCredentials(PMQCSP pCSP)
#if MQPL_NATIVE==MQPL_WINDOWS_NT
{
    HANDLE token;
    BOOL rc = FALSE;
    int toReturn = FALSE;
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
#elif MQPL_NATIVE==MQPL_UNIX
{
    int    rc = FALSE;
    pid_t  amqoampxPID;
    int    amqoampxINFD[2];
    int    amqoampxOUTFD[2];
    char   amqoampxPath[MQ_PATH_MAX + 1] = {0};
    char   response[1000] = {0};
    char * user;
    char * execArgs[2] = {0};
    int    bytesRead = 0;

    /* build path for amqoampx and check it exists */
    strcat(amqoampxPath, DEFAULT_INSTALL_LOCATION);
    strcat(amqoampxPath, "/bin/security/amqoampx");
    if(access(amqoampxPath, X_OK) != 0) {
        /* We can't find and execute AMQOAMAX so we can't check credentials! */
        return FAIL;
    }

    /* Grab some memory to copy the userid into so we can add a null character at the end */
    user = malloc( sizeof(char) * pCSP->CSPUserIdLength + 1);

    if(user != NULL)
    {
        /* Copy userid and password into the malloc'd memory with a null character at the end */
        memset(user, '\0', pCSP->CSPUserIdLength + 1);
        strncpy(user, pCSP->CSPUserIdPtr, pCSP->CSPUserIdLength);

        execArgs[0] = amqoampxPath;
        execArgs[1] = user;

        /* Create two pipes for the input/output and fork here */
        pipe(amqoampxINFD);
        pipe(amqoampxOUTFD);
        amqoampxPID = fork();

        if(amqoampxPID == 0)
        {
            /* This code is ran by the child and starts amqoampx */
            
            /* First wire up the pipes we previously created     */
            dup2(amqoampxOUTFD[0], STDIN_FILENO);
            dup2(amqoampxINFD[1], STDOUT_FILENO);

            /* Now call amqoampx */
            execv(amqoampxPath, execArgs);

            /* If something goes wrong we need to stop here. */
            exit(1);
        }

        /* This code is only ran by the parent */

        /* Close off the other ends of the pipes we don't need in the parent */
        close(amqoampxOUTFD[0]);
        close(amqoampxINFD[1]);
        
        /* Now the parent should write the password to the child (amqoampx) followed by a newline */
        write(amqoampxOUTFD[1], pCSP->CSPPasswordPtr, pCSP->CSPPasswordLength);
        write(amqoampxOUTFD[1], "\n", 1);

        /* Finally the parent should read the response */
        for(int i = 0; ;i++ )
        {
            int readBytes = 0;
            readBytes = read(amqoampxINFD[0], &response[bytesRead], sizeof(response)-1-bytesRead);
            if (readBytes == 0)
                break; /* Done */
            
            if (readBytes > 0)
            {
                bytesRead += readBytes;
                if ( bytesRead == sizeof(response)-1 )
                    break;
            }
            else
            {
                break;
            }
        }

        /* If the response that came back was +0 [NULL] then the credentials were valid! */
        if(response[0] == '+')
        {
            rc = TRUE;
        }
    }

    /* Free any memory we allocated */
    if(user)
    {
        free(user);
    }

    return rc;
}
#else
{
    return FAIL;
}
#endif
