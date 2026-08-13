/*
  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  Copyright (c) IBM Corporation 2026
*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmqc.h>
#include <cmqec.h>
#include <cmqxc.h>

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

#define RoundTo4(u) (MQUINT32)(((u)+ (( 4-((u)%4))%4)))

// Global variables
static FILE *fp = NULL;
static int closeFp = TRUE;

static int initCount = 0;

// The W3C names for the properties to be propagated
#define TRACEPARENT "traceparent"
#define TRACESTATE "tracestate"

// Environment variable names
#define ENV_LOGFILE "APIX_LOGFILE"       // a file, or stdout or stderr
#define ENV_PRESERVE "AMQ_RFH2_PRESERVE" // Keep the RFH2 regardless of other settings

/* Platform-specific mutex types and initialisers */
#ifdef _WIN32
#include <windows.h>
typedef CRITICAL_SECTION common_mutex_t;
INIT_ONCE critSecInitOnce = INIT_ONCE_STATIC_INIT;
#else
#include <pthread.h>
typedef pthread_mutex_t common_mutex_t;
pthread_once_t mutexInitOnce = PTHREAD_ONCE_INIT;
#endif

static common_mutex_t mutex;

#ifdef _WIN32
BOOL CALLBACK InitCritSecCallback(PINIT_ONCE InitOnce, PVOID Parameter, PVOID* Context) {
    InitializeCriticalSection(&mutex);
    return TRUE;
}
#else
void mutexInit(void) {
    pthread_mutex_init(&mutex, NULL);
}
#endif

/*********************************************************************/
/* Declare internal functions. All except the                        */
/* entrypoint can be static as they're not used by any other module. */
/*********************************************************************/
MQ_INIT_EXIT EntryPoint;
static MQ_TERM_EXIT Terminate;

static MQ_GET_EXIT GetAfter;
static MQ_CALLBACK_EXIT CallbackBefore;

static void rpt(char *fmt, ...);

static int lock(common_mutex_t *mutex);
static int unlock(common_mutex_t *mutex);

static size_t strnstr(const char *haystack, const char *needle, size_t len);

// Used by both MQGET and Callback functions. Don't need all of the parameters from the invocation
static void commonGetAfter(PPMQMD ppMsgDesc,
                           PPMQGMO ppGetMsgOpts,
                           PMQLONG pBufferLength,
                           PPMQVOID ppBuffer,
                           PPMQLONG ppDataLength,
                           PMQLONG pCompCode,
                           PMQLONG pReason);

/*********************************************************************/
/* Standard MQ Entrypoint. Not directly used, but                    */
/* required by some platforms.                                       */
/*********************************************************************/
void *MQStart() { return 0; }

/*********************************************************************/
/* Initialisation function.                                          */
/* This is called as an application connects to the queue manager.   */
/*********************************************************************/
void MQENTRY EntryPoint(PMQAXP pExitParms, PMQAXC pExitContext, PMQLONG pCompCode, PMQLONG pReason) {

  char *f = getenv(ENV_LOGFILE);

  int rc = 0;
  char *msg = NULL;
  MQLONG env = pExitContext->Environment;

  pExitParms->ExitResponse = MQXCC_OK;

  /* Initialise the process-wide mutex. This is done by a function that's
     guaranteed to be only called once. But which varies by platform.
  */
#ifdef _WIN32
  InitOnceExecuteOnce(&critSecInitOnce, InitCritSecCallback, NULL, NULL);
#else
  pthread_once(&mutexInitOnce,mutexInit);
#endif

  // Open a log file - we do this first for any tracing option even in 32-bit mode
  lock(&mutex);

  // The Initialisation routine is called for each MQCONN(X) but there are some things we
  // may only want to do (and undo) once. So we maintain a process-wide counter to flag that
  // the corresponding Terminate routine is the last one.
  initCount++;

  if (!fp) {
    if (f) {
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
        rpt("Opened logfile %s", f);
      } else {
        pExitParms->ExitResponse = MQXCC_FAILED;
        strncpy(pExitParms->ExitPDArea, "Cannot open logfile", sizeof(pExitParms->ExitPDArea));
      }
    }
  }
  unlock(&mutex);

  if (pExitParms->ExitResponse != MQXCC_OK) {
    // Couldn't open logfile, so error is directly reported above
    // Don't set msg variable
  } else if (pExitParms->APICallerType != MQXACT_EXTERNAL || env != MQXE_OTHER) {
    msg = "NoOtel Exit: Not supported in qmgr processes";
  } else if (getenv(ENV_PRESERVE) != NULL) {
    msg = "NoOtel Exit: Disabled by environment variable";
  } else {
    lock(&mutex);

    // Only insert our code if the init was successful. Otherwise we will not actually report the error
    /// so that apps that don't match our requirements can still work with this qmgr albeit uninstrumented.
    if (rc == 0) {
      pExitParms->Hconfig->MQXEP_Call(pExitParms->Hconfig, MQXR_AFTER, MQXF_GET, (PMQFUNC)GetAfter, 0, pCompCode, pReason);
      pExitParms->Hconfig->MQXEP_Call(pExitParms->Hconfig, MQXR_BEFORE, MQXF_CALLBACK, (PMQFUNC)CallbackBefore, 0, pCompCode, pReason);
      pExitParms->Hconfig->MQXEP_Call(pExitParms->Hconfig, MQXR_CONNECTION, MQXF_TERM, (PMQFUNC)Terminate, 0, pCompCode, pReason);
    }

    unlock(&mutex);
  }

  if (msg != NULL) {
    strncpy(pExitParms->ExitPDArea, msg, sizeof(pExitParms->ExitPDArea));
    rpt(msg);
  }

  // Continue even if there is an error
  if (rc != 0) {
    // pExitParms->ExitResponse = MQXCC_FAILED;
  }
  return;
}

// Cleanup here needs to be for process-wide resources. So we need to take account of the initCount value
static void Terminate(PMQAXP pExitParms, PMQAXC pExitContext, PMQLONG pCompCode, PMQLONG pReason) {

  lock(&mutex);

  rpt("Terminate: initCount=%d", initCount);

  initCount--;
  if (initCount <= 0) {
    rpt("Terminate: shutting down");

    if (fp) {
      fflush(fp);
      if (closeFp) {
        fclose(fp);
      }
      fp = NULL;
    }

    initCount = 0;
  }
  unlock(&mutex);

  return;
}

static void MQENTRY GetAfter(PMQAXP pExitParms, PMQAXC pExitContext, PMQHCONN pHconn, PMQHOBJ pHobj, PPMQMD ppMsgDesc, PPMQGMO ppGetMsgOpts,
                             PMQLONG pBufferLength, PPMQVOID ppBuffer, PPMQLONG ppDataLength, PMQLONG pCompCode, PMQLONG pReason) {

  commonGetAfter(ppMsgDesc, ppGetMsgOpts, pBufferLength, ppBuffer, ppDataLength, pCompCode, pReason);
  return;
}

static void MQENTRY CallbackBefore(PMQAXP pExitParms, PMQAXC pExitContext, PMQHCONN pHconn, PPMQMD ppMsgDesc, PPMQGMO ppGetMsgOpts, PPMQVOID ppBuffer,
                                   PPMQCBC ppMQCBContext) {
  PMQCBC cbc = *ppMQCBContext;
  PMQLONG pDataLength = &cbc->DataLength;

  // CallbackBefore is similar to GetAfter - it's got the message contents ready for the application to process it. So we can share the
  // real getAfter function. Despite the name of this function. But we do want to check that there's a valid message first
  if (cbc->CallType == MQCBCT_MSG_REMOVED && (cbc->CompCode == MQCC_OK || cbc->Reason == MQRC_TRUNCATED_MSG_ACCEPTED)) {
      commonGetAfter(ppMsgDesc, ppGetMsgOpts, &cbc->BufferLength, ppBuffer, &pDataLength, &cbc->CompCode,
                  &cbc->Reason);
  }
  return;
}

/*
 * myStrnstr - locate a substring within a length-limited string.
 * An implementation of the non-standard strnstr function. Giving it a different
 * name to avoid clashes in case we build this on platforms where the function is available.
 *
 * Searches at most 'len' bytes of 'haystack' for the first occurrence of
 * 'needle'.  Returns the byte offset of the first character of the match,
 * or (size_t)-1 if the needle is not found within the bounded region.
 */
static size_t myStrnstr(const char *haystack, const char *needle, size_t len) {
  size_t needle_len;

  if (needle[0] == '\0') {
    return 0;
  }

  needle_len = strlen(needle);
  if (needle_len > len) {
    return (size_t)-1;
  }

  for (size_t i = 0; i <= len - needle_len; i++) {
    if (memcmp(&haystack[i], needle, needle_len) == 0) {
      return i;
    }
  }

  return (size_t)-1;
}

/* Return the length of the named property. 0 if not found. We don't care about
   the actual value of the property so we don't need to extract that.
*/
size_t extractRFH2PropVal(const char *buf, int buflen, const char *prop) {

  size_t foundLen = 0;

  size_t start = 0;
  size_t end = 0;

  char propXml[64] = {0}; // Long enough for the properties we care about

  snprintf(propXml,sizeof(propXml)-1, "<%s>",prop);

  /* Is there an opening tag with the requested property? */
  start = myStrnstr(buf, propXml, buflen);
  if (start != (size_t)-1) {
    /* Advance past the opening tag so 'end' is relative to buf[start] */
    start += strlen(propXml);
    /* Where does the XML close tag begin */
    snprintf(propXml,sizeof(propXml)-1, "</%s>",prop);

    /* "end" is relative to "start" - it's not an absolute offset in the buffer */
    end = myStrnstr(&buf[start], propXml, buflen - start);
    if (end != (size_t)-1) {
      foundLen = end;
    }
  }

  if (foundLen != 0) {
    rpt("Prop: %s Start:%d End:%d Val:%*.*s",prop, start,end, end,end,&buf[start]);
  } else {
    rpt("Prop: %s Val: Not found",prop);
  }

  return foundLen;
}

static void commonGetAfter(PPMQMD ppMsgDesc, PPMQGMO ppGetMsgOpts, PMQLONG pBufferLength,
                  PPMQVOID ppBuffer, PPMQLONG ppDataLength, PMQLONG pCompCode, PMQLONG pReason) {

  PMQGMO gmo = *ppGetMsgOpts;
  PMQMD md = *ppMsgDesc;
  PMQVOID buffer = *ppBuffer;
  PMQLONG dataLen = *ppDataLength;

  size_t traceparentLen;
  size_t tracestateLen;
  size_t expectedLen;

  MQBOOL haveMsg = TRUE;

  // If the user asked for specific processing for properties, we won't touch the message
  MQLONG gmoSpecificFlags = MQGMO_PROPERTIES_FORCE_MQRFH2 | MQGMO_PROPERTIES_IN_HANDLE | MQGMO_NO_PROPERTIES;

  int removed = 0;

  rpt("> GetAfter");

  if (*pCompCode != MQCC_OK && *pReason != MQRC_TRUNCATED_MSG_ACCEPTED) {
    haveMsg = FALSE;
  }

  if (haveMsg &&
      md &&
      !strncmp(md->Format, MQFMT_RF_HEADER_2, MQ_FORMAT_LENGTH) &&
      (gmo->Options & gmoSpecificFlags) == 0
     ) {

    rpt("Looking for OTel context properties in RFH2");

    PMQRFH2 rfh2 = (PMQRFH2)buffer;
    MQLONG offset = MQRFH_STRUC_LENGTH_FIXED_2;

    int propsLen = rfh2->StrucLength - offset;
    char *b = (char *)buffer;

    /* Need to then step past the MQLONG for the NameValueLen part of the RFH2 properties
       There might be multiple blocks of NameValues, but only the first one is actually relevant
     for our searching.
    */
    offset += 4;
    propsLen -= 4;

    // This is the start of the first properties folder
    char *props = &b[offset];

    traceparentLen = extractRFH2PropVal(props, propsLen, TRACEPARENT);
    tracestateLen = extractRFH2PropVal(props, propsLen, TRACESTATE);

    rpt("Property Lengths - traceparent:%d tracestate:%d", traceparentLen, tracestateLen);

    /* How long do we expect the property block to be if only one or both of the OTel properties
       is in there? TraceState is an optional property, so may not exist.
       Build up the expected length to include opening and closing tags. The RFH2 contents
       look like this:
           <usr><traceparent>12345678</traceparent><tracestate>in=1234567</tracestate></usr>
       And it needs to be a multiple of 4, so we round up.
     */
    expectedLen = strlen("<usr></usr>");
    expectedLen += strlen(TRACEPARENT) * 2 + strlen("<>") + strlen("</>") + traceparentLen;
    if (tracestateLen > 0) {
      expectedLen += strlen(TRACESTATE) * 2 + strlen("<>") + strlen("</>") + tracestateLen;
    }
    expectedLen = RoundTo4(expectedLen);

    rpt("props len: %d ExpectedLen: %d", propsLen, expectedLen);

    /* If the lengths match, then we will remove the RFH2 entirely, adjusting the returned MQMD
       Otherwise, leave it alone.
     */
    if (propsLen == expectedLen) {
      // Fields that need updating in the MQMD
      memcpy(md->Format, rfh2->Format,MQ_FORMAT_LENGTH);
      md->CodedCharSetId = rfh2->CodedCharSetId;
      md->Encoding = rfh2->Encoding;

      // Shuffle the data downwards in the application's buffer
      offset = rfh2->StrucLength;
      memmove(b, &b[offset], *dataLen - offset);

      // Reset the received data lengths
      *dataLen = *dataLen -offset;

      if ((md->Version == MQMD_VERSION_2) && (md->OriginalLength != -1)) {
        md->OriginalLength -= offset;
      }
    }
  } else {
    rpt("Either no message or no suitable RFH2 found. MQRC: %d", *pReason);
  }

  rpt("< GetAfter");
  return;
}

// Simple logger
static void rpt(char *fmt, ...) {
  va_list va;
  va_start(va, fmt);
  size_t l;
  if (fp) {
    fprintf(fp, "NoOtel Exit: ");
    vfprintf(fp, fmt, va);
    l = strlen(fmt);
    if (l > 0 && fmt[l - 1] != '\n') {
      fprintf(fp, "\n");
    }
  }
  va_end(va);
}

static int lock(common_mutex_t *mutex) {
#ifdef _WIN32
  EnterCriticalSection(mutex);
  return 0;
#else
  if (pthread_mutex_lock(mutex) == 0) {
    return 0;
  }
  return -1;
#endif
}

static int unlock(common_mutex_t *mutex) {
#ifdef _WIN32
  LeaveCriticalSection(mutex);
  return 0;
#else
  if (pthread_mutex_unlock(mutex) == 0) {
    return 0;
  }
  return -1;
#endif
}

