/*****************************************************************************/
/*                                                                           */
/* Module Name: chllog.c                                                     */
/*                                                                           */
/* (C) IBM Corporation 2026                                                  */
/*                                                                           */
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
/* Description: A sample channel exit that logs its invocations              */
/*              but otherwise does nothing. It can be used to explore when   */
/*              the different operations are called.                         */
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

#if defined(_WIN32) || defined(WIN32)
  #include <windows.h>
#else
  #include <pthread.h>
#endif

/* IBM MQ includes */
#include <cmqc.h>
#include <cmqxc.h>
#include <cmqstrc.h>

/* Define the entry point. This has to be "public", not static */
MQ_CHANNEL_EXIT ChlExit;

/* Standard constants */
#ifndef FALSE
  #define FALSE (0)
#endif
#ifndef TRUE
  #define TRUE (1)
#endif
#define OK (0)

// Controlling the log file and related functions
#define DEFAULT_LOG_FILE "stderr"
static FILE *fp  = NULL;
int closeFp = TRUE;

#define PADDING16 "                "
#define PADDING32 (PADDING16 PADDING16)
#define PADDING48 (PADDING32 PADDING16)

static void rpt_fn(FILE *fp, const char *format, ...);
/* Calls to rpt() must be done without already holding the exit lock */
#define rpt(fp,format, ...)  { lock(); rpt_fn(fp, format, __VA_ARGS__); unlock(); }

// Keep a count of initialisations in this process so we can cleanup on exit
static int initCount = 0;

#if defined(_WIN32) || defined(WIN32)
static CRITICAL_SECTION cs;
static LONG csInit = 0;

static void lock(void) {
  if (InterlockedCompareExchange(&csInit, 1, 0) == 0) {
    InitializeCriticalSection(&cs);
    csInit = 2;
  } else {
    while (csInit != 2) {
      Sleep(1);
    }
  }
  EnterCriticalSection(&cs);
}

static void unlock(void) {
  LeaveCriticalSection(&cs);
}
#else
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static void lock(void) {
    pthread_mutex_lock(&mutex);
}
static void unlock(void) {
    pthread_mutex_unlock(&mutex);
}
#endif

/***************************************************************************/
/* FUNCTION: logExit                                                       */
/*   Log key elements from a channel exit invocation                       */
/***************************************************************************/
static void logExit(PMQCXP pCXP, PMQCD pCD) {

     // Trim down the string conversions a little
    char *exitType = MQXT_STR(pCXP->ExitId);
    char *exitReason = MQXR_STR(pCXP->ExitReason);
    int exitTypeLen = (int)strlen(exitType);

    // The *_STR functions always return non-NULL. But it might be an empty string.
    if (exitType[0] != 0) {
      exitType += 5;

      if (!strncmp(exitType,"CHANNEL_",8)) {
        exitType += 8;
        exitTypeLen = (int)strlen(exitType);
      }

      int l = exitTypeLen - 5;
      if (l >=0 && !strncmp(&exitType[l],"_EXIT",5)) {
        exitTypeLen -=5;
      }
    } else {
      exitType = "UNKNOWN";
    }

    if (exitReason[0] != 0) {
      exitReason += 5;
    }
    else {
      exitReason = "UNKNOWN";
    }

    rpt(fp, "Chl: %*.*s Exit : %*.*s [%d]",
        MQ_CHANNEL_NAME_LENGTH, MQ_CHANNEL_NAME_LENGTH, pCD->ChannelName,
        exitTypeLen, exitTypeLen, exitType,pCXP->ExitId);
    rpt(fp, "     %*.*s Cause: %s [%d]",  MQ_CHANNEL_NAME_LENGTH, MQ_CHANNEL_NAME_LENGTH, PADDING32,
        exitReason,pCXP->ExitReason);

    return;
}

/***************************************************************************/
/* FUNCTION: logPreConnExit                                                */
/*   Log key elements from a preconnect exit invocation                    */
/***************************************************************************/
static void logPreconnExit(PMQNXP pNXP, MQCHAR48 qMgr) {

     // Trim down the string conversions a little
    char *exitType = MQXT_STR(pNXP->ExitId);
    char *exitReason = MQXR_STR(pNXP->ExitReason);
    int exitTypeLen = (int)strlen(exitType);

    if (exitType[0] != 0) {
      exitType += 5;
      exitTypeLen -=5;
    }
    else {
      exitType = "UNKNOWN";
    }

    if (exitReason[0] != 0) {
      exitReason += 5;
    }
    else {
      exitReason = "UNKNOWN";
    }

    rpt(fp, "QMgr: %s Exit : %*.*s [%d]",
        qMgr?qMgr:"N/A",
        exitTypeLen, exitTypeLen, exitType,pNXP->ExitId);
    rpt(fp, "     Cause: %s [%d]", exitReason,pNXP->ExitReason);

    return;
}

/***************************************************************************/
/* FUNCTION: openLogFile                                                   */
/*   Make sure the log file is open. The actual open is only done once.    */
/*   And there's a mutex to avoid races. A counter is incremented, so we   */
/*   can tell how many initialisations are done.                           */
/*                                                                         */
/*   If the log file cannot be opened, there is a non-zero return code     */
/***************************************************************************/
static int openLogFile() {
    MQBOOL  didOpen = FALSE;
    char   *f = NULL;
    int rc = OK;

    lock();

    if (!fp) {
      f = getenv("CHLLOG_LOG_FILE");
      if (!f) {
        f = DEFAULT_LOG_FILE;
      }

      if (!strcmp(f, "stdout")) {
        fp = stdout;
        closeFp = FALSE;
      } else if (!strcmp(f, "stderr")) {
        fp = stderr;
        closeFp = FALSE;
      } else {
        fp = fopen(f, "a");
      }

      if (fp) {
        setbuf(fp, NULL); /* try to reduce interleaved output; auto-flush */
        didOpen = TRUE;
      } else {
    #if defined(_WIN32) || defined(WIN32)
        char *errbuf;
        errbuf = strerror(errno); /* The Windows version is thread-safe */
    #else
        char errbuf[128] = {0};
        strerror_r(errno, errbuf, sizeof(errbuf)-1); // XSI version by default. In the GNU version, we'd use the returned char * value */
    #endif
        fprintf(stderr, "Cannot open logfile \"%s\" errno: %d [%s]\n",f,errno,errbuf);
        rc = -1;
      }
    }

    if (rc == OK) {
      initCount++;
    }

    unlock();

    if (didOpen) {
      rpt(fp, "Opened logfile %s", f); // Can't call rpt earlier as it would deadlock
    }

    return rc;
}

/***************************************************************************/
/* FUNCTION: closeLogFile                                                  */
/*   Decrement the use count, and if appropriate close the log file.       */
/***************************************************************************/
static void closeLogFile() {
    lock();
    initCount--;
    if (initCount <= 0) {
      if (fp) {
        fflush(fp);
        if (closeFp) {
         fclose(fp);
        }
        fp = NULL;
      }

      initCount = 0;
    }
    unlock();
}

/***************************************************************************/
/* FUNCTION: PreconnectExit                                                */
/*   Entrypoint when being called as a PreConn exit                        */
/***************************************************************************/
void MQENTRY PreconnectExit ( PMQNXP  pExitParms,
                              PMQCHAR pQMgrName,
                              PPMQCNO ppConnectOpts,
                              PMQLONG pCompCode,
                              PMQLONG pReason)
{
  MQCD defaultCD = {MQCD_DEFAULT};
  int rc = OK;

  pExitParms->ExitResponse = MQXCC_OK;
  pExitParms->ExitResponse2 = MQXR2_DEFAULT_CONTINUATION;

  switch (pExitParms->ExitReason) {
  case MQXR_INIT:
     rc = openLogFile();
     logPreconnExit(pExitParms, pQMgrName);
     break;
  case MQXR_TERM:
      logPreconnExit(pExitParms, pQMgrName);
      closeLogFile();
     break;
  default:
    logPreconnExit(pExitParms, pQMgrName);
    rpt(fp, "     Data: %*.*s", pExitParms->ExitDataLength, pExitParms->ExitDataLength, pExitParms->pExitDataPtr);
    break;
  }

  if (rc != OK) {
    *pCompCode = MQCC_FAILED;
    *pReason = MQRC_PRECONN_EXIT_ERROR;
    pExitParms->ExitResponse = MQXCC_SUPPRESS_EXIT;
    pExitParms->ExitResponse2 = MQXR2_CONTINUE_CHAIN;
  }

}

/***************************************************************************/
/* FUNCTION: ChlExit                                                       */
/*   Entrypoint when being called as a channel exit                        */
/*                                                                         */
/* Most of the parameters here are unused in this function. But they are   */
/* needed to match the MQI definition for calling a channel exit.          */
/***************************************************************************/
void MQENTRY ChlExit(PMQVOID pChannelExitParms,
        PMQVOID pChannelDefinition,
        PMQLONG pDataLength,
        PMQLONG pAgentBufferLength,
        PMQVOID pAgentBuffer,
        PMQLONG pExitBufferLength,
        PMQPTR  pExitBufferAddr)
{
  MQLONG  rc  = OK;

  /* Convert the pointers to specific structure types */
  PMQCXP pCXP = (PMQCXP)pChannelExitParms;
  PMQCD  pCD  = (PMQCD)pChannelDefinition;

  /***************************************************************************/
  /* Switch on the reason the exit was called. In particular so we can do    */
  /* any necessary init/term processing                                      */
  /***************************************************************************/
  switch (pCXP->ExitReason) {
  case MQXR_INIT:
    rc = openLogFile();
    logExit(pCXP,pCD);

    break;

  case MQXR_TERM:
    logExit(pCXP,pCD);
    closeLogFile();
    break;

  default:
    logExit(pCXP,pCD);
    break;
  }

  if (rc == OK) {
    // Could now add specific logging for particular exit reasons.
    switch (pCXP->ExitReason) {
    default:
      break;
    }
  }

  // Set a final return code to the caller
  if (rc != OK) {
    pCXP->ExitResponse = MQXCC_SUPPRESS_FUNCTION;
  }
  else {
    pCXP->ExitResponse = MQXCC_OK;
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
  while (p>=line) {
    if (isspace((unsigned char)*p))
      *p=0;
    else
      break;
    p--;
  }

  /* Remove leading spaces */
  p=line;
  while (*p != 0 && isspace((unsigned char)*p)) {
    p++;
  }
  return p;
}

/*********************************************************************/
/*                                                                   */
/* Function Name:  rpt_fn                                            */
/*                                                                   */
/* Description: Writes a timestamp followed by the supplied log      */
/*              message to the supplied file handle.                 */
/*********************************************************************/
static void rpt_fn(FILE * fp, const char * fmt, ...)
{
  time_t   t = time(NULL);
  struct tm * now;
  va_list  arg_ptr;

  if (fp != NULL) {
    va_start(arg_ptr, fmt);
    now = localtime(&t);
    fprintf(fp , "%04d/%02d/%02d %02d:%02d:%02d ",
                       now->tm_year+1900,
                       now->tm_mon+1,
                       now->tm_mday,
                       now->tm_hour,
                       now->tm_min,
                       now->tm_sec );
    vfprintf(fp, fmt, arg_ptr);
    if (fmt[strlen(fmt)-2] != '\n') {
      fprintf(fp,"\n");
    }
    va_end(arg_ptr);
  }
  return;
}

/*********************************************************************/
/* Standard MQ Entrypoint (unused, but some platforms need it)       */
/*********************************************************************/
void MQStart() {
  ;
}