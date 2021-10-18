/*****************************************************************************/
/*                                                                           */
/* Module Name: oamcrt.c                                                     */
/*                                                                           */
/* (C) IBM Corporation 2021                                                  */
/*                                                                           */
/* AUTHOR: Mark Taylor, IBM Hursley                                          */
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
/* PLEASE NOTE - This code is supplied "AS IS" with no                       */
/*              warranty or liability. It is not part of                     */
/*              any product.                                                 */
/*                                                                           */
/* Description: A sample Authorisation Service for IBM MQ                    */
/*                                                                           */
/*   This module implements the MQ Authorisation Service API, defined        */
/*   in the Programmable System Management book.  It can be used to augment  */
/*   the default Object Authority Manager to apply additional rules on which */
/*   named queues a user is allowed to create.                               */
/*                                                                           */
/*   See the readme supplied with this module for compilation,               */
/*   configuration and usage information.                                    */
/*                                                                           */
/*****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

extern int getpid(void);
extern pid_t gettid(void);

/***********************************************************************/
/* The MQI entry points appear via pointers passed to the exit         */
/* instead of being explicitly named.                                  */
/***********************************************************************/
#include <cmqec.h>     /* Added for V7.1 function pointers             */
#define MQZEP hc->MQZEP_Call

/***********************************************************************/
/* This header file is required always                                 */
/***********************************************************************/
#include <cmqcfc.h>    /* PCF definitions needed for Inquire           */

#if MQAT_DEFAULT == MQAT_UNIX
#include <pthread.h>
#else
#error Unsupported platform
#endif


/*****************************************************************************/
/* This module must be run before the default OAM, with the chain continuing */
/* unless we have decided to reject a request.                               */
/*                                                                           */
/* Some functions - eg Init, Term and Refresh - will continue the chain      */
/* if a service component returns MQCC_OK but most do not. So I'll define    */
/* the return code which will force the chain to continue unless we want to  */
/* reject an operation.                                                      */
/*                                                                           */
/* The only MQRC error values which are explicitly tested by the code that   */
/* invokes the Authorisation Service are                                     */
/*   MQRC_NOT_AUTHORIZED                                                     */
/*   MQRC_SERVICE_ERROR                                                      */
/*   MQRC_SERVICE_NOT_AVAILABLE                                              */
/*   MQRC_UNKNOWN_ENTITY                                                     */
/*   MQRC_UNKNOWN_OBJECT_NAME                                                */
/* A real auth service should normally set one of these. If the MQRC is not  */
/* one of these, then 'default' processing (which may vary by operation)     */
/* will be done.                                                             */
/*****************************************************************************/
#define OA_DEF_CC (MQCC_WARNING)

/****************************************************************************/
/* Declare the internal functions that implement the interface              */
/* The "_2" versions are needed for the MQZED structure to be passed. They  */
/* are automatically selected when we specify the SupportedVersion value    */
/* during initialisation.                                                   */
/****************************************************************************/
MQZ_INIT_AUTHORITY                  MQStart;
static MQZ_TERM_AUTHORITY           OATermAuth;
static MQZ_COPY_ALL_AUTHORITY       OACopyAllAuth;
static MQZ_CHECK_AUTHORITY_2        OACheckAuth;
static MQZ_REFRESH_CACHE            OARefreshCache;

static MQLONG readConfig(char *);
static MQBOOL permittedObject(PMQCHAR objectName,PMQCHAR pUserName);

static char *trim(char *s);
static void rpt(char *func,char *fmt,...);

#ifndef TRUE
#define TRUE (1)
#define FALSE (0)
#endif

/****************************************************************************/
/* This is where we'll write the logged information.                        */
/****************************************************************************/
#define LOGFILE "/var/mqm/audit/oamcrt.log"
/* And the config file is here */
#define CONFIGFILE "/var/mqm/audit/oamcrt.ini"

/****************************************************************************/
/* A group  of process-wide global variables.                               */
/****************************************************************************/
int primary_process = 0;
FILE *fp = NULL;

static MQLONG SupportedVersion = 0;
static MQBOOL alreadyRead = FALSE;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/****************************************************************************/
/* And some thread-specific variables that require appropriate support from */
/* the compiler. This allows us to pass a small amount of information       */
/* between operations as the CopyAll will always follow a CheckAuth on the  */
/* same thread when opening a model queue.                                  */
/****************************************************************************/
static __thread MQBOOL  prepareToCopy = FALSE;
static __thread PMQCHAR prepareToCopyUser = NULL;

/*************************************************************************/
/* This is where I'd put the code to read a config file. It would list   */
/* usernames and permitted patterns for a queue name                     */
/*************************************************************************/
static MQLONG readConfig(char *file) {
  MQLONG CC = MQCC_OK;
  if (!alreadyRead) {
    if (pthread_mutex_lock(&mutex) == 0) {
      alreadyRead = TRUE;
      pthread_mutex_unlock(&mutex);
    }
  }
  return CC;
}

/*************************************************************************/
/* And this is the code to do the matching against those loaded patterns.*/
/* For now, we just compare with NOTALLOWED to demonstrate the principle.*/
/*                                                                       */
/* In a real environment we'd probably also have a check for the 'mqm'   */
/* user hardcoded here just in case it's not in any external config.     */
/*************************************************************************/
static MQBOOL permittedObject(PMQCHAR pObjectName,PMQCHAR pUserName) {
  MQBOOL rc = TRUE;

  if (pthread_mutex_lock(&mutex) == 0) { // Don't want config changing under us
    if (strncmp(pObjectName,"NOTALLOWED",10)==0) {
      rc = FALSE;
    }
    pthread_mutex_unlock(&mutex);
  }
  return rc;
}

/****************************************************************************/
/* Function: OARefreshCache                                                 */
/*                                                                          */
/* Description:                                                             */
/*   This tells the OAM to refresh                                          */
/*   any cache of userid/group/authorisation information that it might hold.*/
/*                                                                          */
/* Note:                                                                    */
/*   This function CAN return MQCC_OK as the queue manager invokes all      */
/*   service components in the chain, whatever the return code.             */
/****************************************************************************/
static void MQENTRY OARefreshCache(
  PMQCHAR  pQMgrName,
  PMQBYTE  pComponentData,
  PMQLONG  pContinuation,
  PMQLONG  pCompCode,
  PMQLONG  pReason)
{
  /* Reread the configuration file */
  alreadyRead = FALSE;
  (void)readConfig(CONFIGFILE);

  *pCompCode = MQCC_OK;
  *pReason   = MQRC_NONE;
  *pContinuation = MQZCI_CONTINUE;
  return;
}

/****************************************************************************/
/* Function: OACopyAllAuth                                                  */
/*                                                                          */
/* Description:                                                             */
/*   Sets the authority for a new object, based on a reference set of       */
/*   permissions. In fact, it's only called (today) during                  */
/*   creation of a dynamic queue.                                           */
/****************************************************************************/
static void MQENTRY OACopyAllAuth(
  PMQCHAR  pQMgrName,
  PMQCHAR  pRefObjectName,
  PMQCHAR  pObjectName,
  MQLONG   ObjectType,
  PMQBYTE  pComponentData,
  PMQLONG  pContinuation,
  PMQLONG  pCompCode,
  PMQLONG  pReason)
{
  /* Write some log information if this looks plausible */
  if ((ObjectType == MQOT_Q || ObjectType == MQOT_MODEL_Q) && strncmp(pObjectName,"SYSTEM.",7) != 0) {
    rpt("OACopyAllAuth", "Prep = %d ObjectType = %d Name = '%-48.48s' User = %s",
               prepareToCopy,
               ObjectType,
               pObjectName,
               prepareToCopyUser?prepareToCopyUser:"<NULL>");

  }

  /****************************************************************************/
  /* Check whether the user is allowed to create the dynamic object           */
  /* The prepareToCopy variables have been set if it looks like this is going */
  /* to happen, including stashing the userid associated with the request     */
  /* Regardless of the outcome, clear out the thread-related stash            */
  /****************************************************************************/
  if (prepareToCopy && !permittedObject(pObjectName,prepareToCopyUser)) {
      *pCompCode = MQCC_FAILED;
      *pReason = MQRC_NOT_AUTHORIZED;
      *pContinuation = MQZCI_STOP;
  } else {
    *pCompCode = OA_DEF_CC;
    *pReason   = MQRC_NONE;
    *pContinuation = MQZCI_CONTINUE;
  }

  prepareToCopy = FALSE;
  if (prepareToCopyUser != NULL) {
    free(prepareToCopyUser);
    prepareToCopyUser = NULL;
  }

  return;
}

/****************************************************************************/
/* Function: OACheckAuth                                                    */
/*                                                                          */
/* Description:                                                             */
/*   Called whenever the qmgr wants to see if someone can do something.     */
/*   If a real object is being created, we can test the permission directly */
/*   If a dynamic queue is being created, we guess that's what is in        */
/*     progress and stash the username for the next operation               */
/****************************************************************************/
static void MQENTRY OACheckAuth (
  PMQCHAR  pQMgrName,
  PMQZED   pEntityData,
  MQLONG   EntityType,
  PMQCHAR  pObjectName,
  MQLONG   ObjectType,
  MQLONG   Authority,
  PMQBYTE  pComponentData,
  PMQLONG  pContinuation,
  PMQLONG  pCompCode,
  PMQLONG  pReason)
{
  /* Just log things that have a chance of being relevant */
  if ((ObjectType == MQOT_Q && strncmp(pObjectName,"SYSTEM.",7) != 0) || (ObjectType == MQOT_MODEL_Q)){
    rpt("OACheckAuth", "Prep = %d ObjectType = %d Name = '%-48.48s' Auth = %08X",
               prepareToCopy,
               ObjectType,
               pObjectName,
               Authority);
  }


  /* Set return values assuming OK */
  *pCompCode = OA_DEF_CC;
  *pReason   = MQRC_UNKNOWN_OBJECT_NAME;
  *pContinuation = MQZCI_CONTINUE;

  prepareToCopy = FALSE;
  if (ObjectType == MQOT_Q) {
    if (Authority == MQZAO_CREATE && !permittedObject(pObjectName,pEntityData->EntityNamePtr)){
      *pCompCode = MQCC_FAILED;
      *pReason = MQRC_NOT_AUTHORIZED;
      *pContinuation = MQZCI_STOP;
    }
  } else if (ObjectType == MQOT_MODEL_Q && Authority == MQZAO_DISPLAY) {
    /* This check is allowed but we note it ready for the copyall later. Also stash */
    /* the username as it's not passed to the copyall                               */
    prepareToCopy = TRUE;
    prepareToCopyUser = strdup(pEntityData->EntityNamePtr);
  }

  return;
}

/****************************************************************************/
/* Function: OATerm                                                         */
/*                                                                          */
/* Description:                                                             */
/*   The function is invoked during various termination stages (look at     */
/*   the Options field for PRIMARY/SECONDARY flags).                        */
/****************************************************************************/
static void MQENTRY OATerm(
  MQHCONFIG  hc,
  MQLONG     Options,
  PMQCHAR    pQMgrName,
  PMQBYTE    pComponentData,
  PMQLONG    pCompCode,
  PMQLONG    pReason)
{

  /* Don't close the logfile if we're in the primary process unless  */
  /* it is also a primary shutdown                                   */
  if ((primary_process && Options == MQZTO_PRIMARY) || !primary_process)
  {
    if (fp)
    {
      fclose(fp);
      fp = NULL;
    }
  }

  *pCompCode = MQCC_OK;
  *pReason   = MQRC_NONE;
  return;
}

/****************************************************************************/
/* Function: MQStart                                                        */
/*                                                                          */
/* Description:                                                             */
/*   This is the initialisation and entrypoint for the dynamically loaded   */
/*   authorisation installable service. It registers the addresses of the   */
/*   other functions which are to be called by the qmgr.                    */
/*                                                                          */
/*   Because of when the init function is called, there is no need to       */
/*   worry about multi-threaded stuff in this particular function, and      */
/*   hence no need to do something like a lock around the fopen() call.     */
/*                                                                          */
/* Note:                                                                    */
/*   This function MUST be called MQStart on some platforms, so we'll use   */
/*   the same name for all of them.                                         */
/****************************************************************************/
void MQENTRY MQStart(
  MQHCONFIG hc,
  MQLONG    Options,
  MQCHAR48  QMgrName,
  MQLONG    ComponentDataLength,
  PMQBYTE   ComponentData,
  PMQLONG   Version,
  PMQLONG   pCompCode,
  PMQLONG   pReason)
{
  MQLONG CC       = MQCC_OK;
  MQLONG Reason   = MQRC_NONE;

  /**************************************************************************/
  /* Only 1 process (amqzxma0) gets this per qmgr but that process may      */
  /* still also get a 2ary init later so we don't have an 'else' clause -   */
  /* the default has already been set at the top of the file.               */
  /**************************************************************************/
  if (Options == 0)
    primary_process = 1;

  if (!fp)
  {
    fp = fopen(LOGFILE,"a"); /* File may need to already exist */
    if (fp)
    {
      setbuf(fp,NULL);  /* try to reduce interleaved output; auto-flush */
    }
    else
    {
	    CC = MQCC_FAILED;
    }
  }

  rpt("OAInit","%s","MQStart invoked");

  /************************************************************************/
  /* Initial load of the config file if it's not already been done        */
  /************************************************************************/
  CC = readConfig(CONFIGFILE);

  /************************************************************************/
  /* Initialise the entry point vectors.  This is performed for both      */
  /* global and process initialisation, ie whatever the value of the      */
  /* Options field.                                                       */
  /************************************************************************/
  if (CC == MQCC_OK)
    MQZEP(hc, MQZID_INIT_AUTHORITY,(PMQFUNC)MQStart,&CC,&Reason);

  if (CC == MQCC_OK)
    MQZEP(hc,MQZID_TERM_AUTHORITY,(PMQFUNC)OATerm,&CC,&Reason);

  if (CC == MQCC_OK)
    MQZEP(hc,MQZID_CHECK_AUTHORITY,(PMQFUNC)OACheckAuth,&CC,&Reason);

  if (CC == MQCC_OK)
    MQZEP(hc,MQZID_COPY_ALL_AUTHORITY,(PMQFUNC)OACopyAllAuth,&CC,&Reason);

  if (CC == MQCC_OK)
    MQZEP(hc,MQZID_REFRESH_CACHE,(PMQFUNC)OARefreshCache,&CC,&Reason);

  if (CC != MQCC_OK)
  {
    CC       = MQCC_FAILED;
    Reason   = MQRC_INITIALIZATION_FAILED;
  }

  /**************************************************************************/
  /* There have been several versions of the Auth Service interface.        */
  /* We will tell the qmgr to call us with the V2 parameters                */
  /**************************************************************************/
  SupportedVersion   = MQZAS_VERSION_6;
  *Version   = SupportedVersion;

  /**************************************************************************/
  /* And set the return codes.                                              */
  /**************************************************************************/
  *pCompCode = CC;
  *pReason   = Reason;

  return;
}

/****************************************************************************/
/* Remove trailing spaces from a string                                     */
/****************************************************************************/
static char *trim(char *s)
{
	int i;
	for (i=strlen(s)-1;i>=0;i--)
	{
		if (s[i] == ' ')
			s[i] = 0;
		else
			break;
	}
	return s;
}

/****************************************************************************/
/* Report the activity if the log file is open                              */
/* Make sure the output does end with '\n'                                  */
/****************************************************************************/
#define TIMEBUF 26 /* The buffer length required for ctime_r */
static char *v_ctime(char *tb)
{
  time_t t = time(NULL);
  return ctime_r(&t,tb);
}
static void rpt(char *func, char *fmt,...)
{
  char tb[TIMEBUF];

  va_list va;
  va_start(va,fmt);
  int l;
  if (fp) {
    fprintf(fp, "%d.%d @ %24.24s %s: ",getpid(),gettid(),v_ctime(tb),func);

    vfprintf(fp,fmt,va);
    l = strlen(fmt);
    if (l>0 && fmt[l-1] != '\n')
    {
      fprintf(fp,"\n");
    }
  }
  va_end(va);
}
