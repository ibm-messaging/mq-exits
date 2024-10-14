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

  Copyright (c) IBM Corporation 2024
*/

#include <dlfcn.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cmqc.h>
#include <cmqec.h>
#include <cmqxc.h>

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

static void rpt(char *fmt, ...);

FILE *fp = NULL;
int closeFp = TRUE;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

#define ENV_LOGFILE "APIX_LOGFILE"
#define ENV_WRAPPER "AMQ_OTEL_INSTRUMENTED"

#define DLMODULE "mqioteldl.so"
static void *hdl = NULL;
static int initCount = 0;

typedef MQLONG OTEL_INIT(void *, char *, size_t);
typedef void OTEL_TERM();

#if defined MQ_64_BIT
#define BITNESS 64
#else
#define BITNESS 32
#endif

/*********************************************************************/
/* Standard MQ Entrypoint. Not directly used, but                    */
/* required by some platforms.                                       */
/*********************************************************************/
void *MQStart() { return 0; }

static void lock() { pthread_mutex_lock(&mutex); }
static void unlock() { pthread_mutex_unlock(&mutex); }

/*********************************************************************/
/* Declare internal functions. All except the                        */
/* entrypoint can be static as they're not used by any other module. */
/*********************************************************************/
MQ_INIT_EXIT EntryPoint;
static MQ_TERM_EXIT Terminate;
static MQ_PUT_EXIT PutBefore;
static MQ_PUT_EXIT PutAfter;
static MQ_PUT1_EXIT Put1Before;
static MQ_PUT1_EXIT Put1After;

static MQ_GET_EXIT GetBefore;
static MQ_GET_EXIT GetAfter;
static MQ_CB_EXIT CBBefore;
static MQ_CALLBACK_EXIT CallbackBefore;

static MQ_OPEN_EXIT OpenAfter;
static MQ_CLOSE_EXIT CloseAfter;

static MQ_DISC_EXIT DiscBefore;

// These are function pointers to the dynamically loaded OTel module
struct {
  OTEL_INIT *init;
  OTEL_TERM *term;

  MQ_OPEN_EXIT *openAfter;
  MQ_CLOSE_EXIT *closeAfter;
  MQ_DISC_EXIT *discBefore;

  MQ_PUT_EXIT *putBefore;
  MQ_PUT_EXIT *putAfter;

  MQ_GET_EXIT *getBefore;
  MQ_GET_EXIT *getAfter;
} ot;

#define DLSYM(FUNC, Name)                                                                                                                                      \
  {                                                                                                                                                            \
    FUNC = (void *)dlsym(hdl, Name);                                                                                                                           \
    if (FUNC == NULL) {                                                                                                                                        \
      rpt("Cannot find symbol %s", Name);                                                                                                                      \
      rc++;                                                                                                                                                    \
    }                                                                                                                                                          \
  }

// So we don't have to keep modifying the dlopen options
#define DLOPEN(mod) dlopen(mod, RTLD_LOCAL | RTLD_NOW)

/*********************************************************************/
/* Initialisation function.                                          */
/* This is called as an application connects to the queue manager.   */
/*********************************************************************/
void MQENTRY EntryPoint(PMQAXP pExitParms, PMQAXC pExitContext, PMQLONG pCompCode, PMQLONG pReason) {

  char *f = getenv(ENV_LOGFILE);
  char modname[PATH_MAX];

  int rc = 0;
  char *p;
  char *msg = NULL;
  MQLONG env = pExitContext->Environment;

  pExitParms->ExitResponse = MQXCC_OK;

  // Open a log file - we do this first for any tracing option even in 32-bit mode
  lock();

  // The Initialisation routine is called for each MQCONN(X) but there are some things we
  // only want to do (and undo) once. So we maintain a process-wide counter to flag that
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
  unlock();

  // rpt("CallerType: %d Env: %d", pExitParms->APICallerType, pExitContext->Environment);

  if (pExitParms->ExitResponse != MQXCC_OK) {
    // Couldn't open logfile, so error is directly reported above
    // Don't set msg variable
  } else if (getenv(ENV_WRAPPER)) {
    // This env var is set by higher layers like the Go and Node wrappers so we don't
    // need to double-dip. It's MY convention, not an official MQ external.
    msg = "OTel Exit: Already instrumented by wrapper"; // 42 chars?
  } else if (BITNESS != 64) {
    msg = "OTel Exit: Not supported in 32-bit apps";
  } else if (pExitParms->APICallerType != MQXACT_EXTERNAL || env != MQXE_OTHER) {
    msg = "OTel Exit: Not supported in qmgr processes";
  } else {
    lock();

    // Use dlopen to pull in the real exit module. Try a number of standard paths, starting
    // with the unqualified version that takes account of LD_LIBRARY_PATH
    if (!hdl) {
      if (!hdl) {
        sprintf(modname, "%s", DLMODULE);
        hdl = DLOPEN(modname);
      }
      if (!hdl) {
        sprintf(modname, "%s/%s", "/var/mqm/exits64", DLMODULE);
        hdl = DLOPEN(modname);
      }
      if (!hdl) {
        p = getenv("MQ_INSTALLATION_NAME");
        if (p) {
          sprintf(modname, "%s/%s/%s", "/var/mqm/exits64", p, DLMODULE);
          hdl = DLOPEN(modname);
        }
      }

      if (!hdl) {
        p = getenv("MQ_DATA_PATH");
        if (p) {
          sprintf(modname, "%s/%s/%s", p, "exits64", DLMODULE);
          hdl = DLOPEN(modname);
        }
      }
      if (!hdl) {
        p = getenv("MQ_DATA_PATH");
        char *p2 = getenv("MQ_INSTALLATION_NAME");
        if (p && p2) {
          sprintf(modname, "%s/%s/%s/%s", p, "exits64", p2, DLMODULE);
          hdl = DLOPEN(modname);
        }
      }
      if (hdl) {
        rpt("Successfully loaded %s", modname);
      }
    } else {
      rpt("Already loaded %s", DLMODULE);
    }

    if (!hdl) {
      // Continue, even if we can't load the OTel module. Don't set any error.
      rpt("WARNING: Cannot load \"%s\" because: %s", DLMODULE, dlerror());
    } else {
      // Fill in the indirect function pointers

      DLSYM(ot.init, "mqotInit"); // Any initialisation needed?
      DLSYM(ot.term, "mqotTerm"); // Any initialisation needed?

      DLSYM(ot.openAfter, "mqotOpenAfter");
      DLSYM(ot.closeAfter, "mqotCloseAfter");
      DLSYM(ot.discBefore, "mqotDiscBefore");

      DLSYM(ot.putBefore, "mqotPutBefore");
      DLSYM(ot.putAfter, "mqotPutAfter");
      DLSYM(ot.getBefore, "mqotGetBefore");
      DLSYM(ot.getAfter, "mqotGetAfter");

      // Do any initialisation. Pass a reference to the logging output function.
      char buf[128]; // May be longer than PD Areab
      if (ot.init) {
        rc = ot.init(rpt, buf, sizeof(buf));
        if (rc == MQRC_ALREADY_CONNECTED) {
          rc = MQRC_NONE;
        }
        msg = buf;
      }
    }

    // Only insert our code if the init was successful. Otherwise we will not actually report the error
    /// so that apps that don't match our requirements can still work with this qmgr albeit uninstrumented.
    if (rc == 0) {
      pExitParms->Hconfig->MQXEP_Call(pExitParms->Hconfig, MQXR_AFTER, MQXF_OPEN, (PMQFUNC)OpenAfter, 0, pCompCode, pReason);
      pExitParms->Hconfig->MQXEP_Call(pExitParms->Hconfig, MQXR_AFTER, MQXF_CLOSE, (PMQFUNC)CloseAfter, 0, pCompCode, pReason);
      pExitParms->Hconfig->MQXEP_Call(pExitParms->Hconfig, MQXR_BEFORE, MQXF_PUT, (PMQFUNC)PutBefore, 0, pCompCode, pReason);
      pExitParms->Hconfig->MQXEP_Call(pExitParms->Hconfig, MQXR_AFTER, MQXF_PUT, (PMQFUNC)PutAfter, 0, pCompCode, pReason);
      pExitParms->Hconfig->MQXEP_Call(pExitParms->Hconfig, MQXR_BEFORE, MQXF_PUT1, (PMQFUNC)Put1Before, 0, pCompCode, pReason);
      pExitParms->Hconfig->MQXEP_Call(pExitParms->Hconfig, MQXR_AFTER, MQXF_PUT1, (PMQFUNC)Put1After, 0, pCompCode, pReason);
      pExitParms->Hconfig->MQXEP_Call(pExitParms->Hconfig, MQXR_BEFORE, MQXF_GET, (PMQFUNC)GetBefore, 0, pCompCode, pReason);
      pExitParms->Hconfig->MQXEP_Call(pExitParms->Hconfig, MQXR_AFTER, MQXF_GET, (PMQFUNC)GetAfter, 0, pCompCode, pReason);
      pExitParms->Hconfig->MQXEP_Call(pExitParms->Hconfig, MQXR_BEFORE, MQXF_CB, (PMQFUNC)CBBefore, 0, pCompCode, pReason);
      pExitParms->Hconfig->MQXEP_Call(pExitParms->Hconfig, MQXR_BEFORE, MQXF_CALLBACK, (PMQFUNC)CallbackBefore, 0, pCompCode, pReason);
      pExitParms->Hconfig->MQXEP_Call(pExitParms->Hconfig, MQXR_AFTER, MQXF_DISC, (PMQFUNC)DiscBefore, 0, pCompCode, pReason);
      pExitParms->Hconfig->MQXEP_Call(pExitParms->Hconfig, MQXR_CONNECTION, MQXF_TERM, (PMQFUNC)Terminate, 0, pCompCode, pReason);
    }

    unlock();
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

  lock();

  rpt("Terminate: initCount=%d", initCount);

  initCount--;
  if (initCount <= 0) {

    if (ot.term) {
      ot.term();
    }

    if (fp) {
      fflush(fp);
      if (closeFp) {
        fclose(fp);
      }
      fp = NULL;
    }

    if (hdl) {
      dlclose(hdl);
      hdl = NULL;
    }
    initCount = 0;
  }
  unlock();

  return;
}

// These functions are minimal - they pass parameters to the real work in the dynamically-loaded module.
// It also allows some of the operations to share - so Put and Put1 both do the same thing in the OTel processing
static void OpenAfter(PMQAXP pExitParms, PMQAXC pExitContext, PMQHCONN pHconn, PPMQOD ppObjDesc, PMQLONG pOptions, PPMQHOBJ ppHobj, PMQLONG pCompCode,
                      PMQLONG pReason) {
  if (ot.openAfter) {
    ot.openAfter(pExitParms, pExitContext, pHconn, ppObjDesc, pOptions, ppHobj, pCompCode, pReason);
  }
  return;
}

static void MQENTRY CloseAfter(PMQAXP pExitParms, PMQAXC pExitContext, PMQHCONN pHconn, PPMQHOBJ ppHobj, PMQLONG pOptions, PMQLONG pCompCode, PMQLONG pReason) {
  if (ot.closeAfter) {
    ot.closeAfter(pExitParms, pExitContext, pHconn, ppHobj, pOptions, pCompCode, pReason);
  }
  return;
}

static void MQENTRY DiscBefore(PMQAXP pExitParms, PMQAXC pExitContext, PPMQHCONN ppHconn, PMQLONG pCompCode, PMQLONG pReason) {
  if (ot.discBefore) {
    ot.discBefore(pExitParms, pExitContext, ppHconn, pCompCode, pReason);
  }
  return;
}

static void MQENTRY PutBefore(PMQAXP pExitParms, PMQAXC pExitContext, PMQHCONN pHconn, PMQHOBJ pHobj, PPMQMD ppMsgDesc, PPMQPMO ppPutMsgOpts,
                              PMQLONG pBufferLength, PPMQVOID ppBuffer, PMQLONG pCompCode, PMQLONG pReason) {
  if (ot.putBefore) {
    ot.putBefore(pExitParms, pExitContext, pHconn, pHobj, ppMsgDesc, ppPutMsgOpts, pBufferLength, ppBuffer, pCompCode, pReason);
  }
  return;
}
static void MQENTRY PutAfter(PMQAXP pExitParms, PMQAXC pExitContext, PMQHCONN pHconn, PMQHOBJ pHobj, PPMQMD ppMsgDesc, PPMQPMO ppPutMsgOpts,
                             PMQLONG pBufferLength, PPMQVOID ppBuffer, PMQLONG pCompCode, PMQLONG pReason) {
  if (ot.putAfter) {
    ot.putAfter(pExitParms, pExitContext, pHconn, pHobj, ppMsgDesc, ppPutMsgOpts, pBufferLength, ppBuffer, pCompCode, pReason);
  }
  return;
}

static void MQENTRY Put1Before(PMQAXP pExitParms, PMQAXC pExitContext, PMQHCONN pHconn, PPMQOD pHobjDesc, PPMQMD ppMsgDesc, PPMQPMO ppPutMsgOpts,
                               PMQLONG pBufferLength, PPMQVOID ppBuffer, PMQLONG pCompCode, PMQLONG pReason) {
  // We need a way to stash info between the BEFORE/AFTER phases based on hObj which does not exist in MQPUT1
  // As nothing else can happen on this hConn between the BEFORE and AFTER, using a dummy hobj is fine. So we can use the same core code
  // for both PUT and PUT1 operations.
  MQHOBJ dummy = MQHO_UNUSABLE_HOBJ;
  if (ot.putBefore) {
    ot.putBefore(pExitParms, pExitContext, pHconn, &dummy, ppMsgDesc, ppPutMsgOpts, pBufferLength, ppBuffer, pCompCode, pReason);
  }
  return;
}
static void MQENTRY Put1After(PMQAXP pExitParms, PMQAXC pExitContext, PMQHCONN pHconn, PPMQOD pHobjDesc, PPMQMD ppMsgDesc, PPMQPMO ppPutMsgOpts,
                              PMQLONG pBufferLength, PPMQVOID ppBuffer, PMQLONG pCompCode, PMQLONG pReason) {
  MQHOBJ dummy = MQHO_UNUSABLE_HOBJ;
  if (ot.putAfter) {
    ot.putAfter(pExitParms, pExitContext, pHconn, &dummy, ppMsgDesc, ppPutMsgOpts, pBufferLength, ppBuffer, pCompCode, pReason);
  }
  return;
}

static void MQENTRY GetBefore(PMQAXP pExitParms, PMQAXC pExitContext, PMQHCONN pHconn, PMQHOBJ pHobj, PPMQMD ppMsgDesc, PPMQGMO ppGetMsgOpts,
                              PMQLONG pBufferLength, PPMQVOID ppBuffer, PPMQLONG ppDataLength, PMQLONG pCompCode, PMQLONG pReason) {
  // All synchronous MQGETs can share the same message handle
  MQHOBJ dummy = MQHO_UNUSABLE_HOBJ;
  if (ot.getBefore) {
    ot.getBefore(pExitParms, pExitContext, pHconn, &dummy, ppMsgDesc, ppGetMsgOpts, pBufferLength, ppBuffer, ppDataLength, pCompCode, pReason);
  }
  return;
}

static void MQENTRY CBBefore(PMQAXP pExitParms, PMQAXC pExitContext, PMQHCONN pHconn, PMQLONG pOperation, PPMQCBD ppCallbackDesc, PMQHOBJ pHobj,
                             PPMQMD ppMsgDesc, PPMQGMO ppGetMsgOpts, PMQLONG pCompCode, PMQLONG pReason) {
  // Can reuse the same code as GetBefore except we don't have the buffer (which we don't care about anyway)
  MQLONG dummy;
  PMQLONG pdummy = &dummy;
  PMQCBD cbd = *ppCallbackDesc;
  PMQGMO gmo = *ppGetMsgOpts;

  if (cbd->CallbackType == MQCBT_MESSAGE_CONSUMER && gmo != NULL) {
    if (ot.getBefore) {
      ot.getBefore(pExitParms, pExitContext, pHconn, pHobj, ppMsgDesc, ppGetMsgOpts, &dummy, NULL, &pdummy, pCompCode, pReason);
    }
  }
  return;
}

static void MQENTRY GetAfter(PMQAXP pExitParms, PMQAXC pExitContext, PMQHCONN pHconn, PMQHOBJ pHobj, PPMQMD ppMsgDesc, PPMQGMO ppGetMsgOpts,
                             PMQLONG pBufferLength, PPMQVOID ppBuffer, PPMQLONG ppDataLength, PMQLONG pCompCode, PMQLONG pReason) {

  MQHOBJ dummy = MQHO_UNUSABLE_HOBJ;
  if (ot.getAfter) {
    ot.getAfter(pExitParms, pExitContext, pHconn, &dummy, ppMsgDesc, ppGetMsgOpts, pBufferLength, ppBuffer, ppDataLength, pCompCode, pReason);
  }
  return;
}

static void MQENTRY CallbackBefore(PMQAXP pExitParms, PMQAXC pExitContext, PMQHCONN pHconn, PPMQMD ppMsgDesc, PPMQGMO ppGetMsgOpts, PPMQVOID ppBuffer,
                                   PPMQCBC ppMQCBContext) {
  PMQCBC cbc = *ppMQCBContext;
  PMQLONG pDataLength = &cbc->DataLength;

  // CallbackBefore is similar to GetAfter - it's got the message contents ready for the application to process it. So we can share the
  // ot.getAfter function. Despite the name of this function. But we do want to check that there's a valid message first
  if (cbc->CallType == MQCBCT_MSG_REMOVED && (cbc->CompCode == MQCC_OK || cbc->Reason == MQRC_TRUNCATED_MSG_ACCEPTED)) {
    if (ot.getAfter) {
      ot.getAfter(pExitParms, pExitContext, pHconn, &cbc->Hobj, ppMsgDesc, ppGetMsgOpts, &cbc->BufferLength, ppBuffer, &pDataLength, &cbc->CompCode,
                  &cbc->Reason);
    }
  }
  return;
}

// Simple logger - also used by the C++ aspect of this exit
static void rpt(char *fmt, ...) {
  va_list va;
  va_start(va, fmt);
  int l;
  if (fp) {
    fprintf(fp, "OTel Exit: ");
    vfprintf(fp, fmt, va);
    l = strlen(fmt);
    if (l > 0 && fmt[l - 1] != '\n') {
      fprintf(fp, "\n");
    }
  }
  va_end(va);
}
