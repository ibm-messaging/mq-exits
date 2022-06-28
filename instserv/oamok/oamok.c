/*****************************************************************************/
/*                                                                           */
/* Module Name: oamok.c                                                      */
/*                                                                           */
/* (C) IBM Corporation 2022                                                  */
/*                                                                           */
/* AUTHOR: Mark Taylor, IBM Hursley                                          */
/*                                                                           */
/* PLEASE NOTE - This code is supplied "AS IS" with no                       */
/*              warranty or liability. It is not part of                     */
/*              any product.                                                 */
/*                                                                           */
/* Description: A sample Authorisation Service for IBM MQ for Auditing       */
/*                                                                           */
/* See the readme supplied in this directory for compilation,                */
/* configuration and usage information.                                      */
/*                                                                           */
/*****************************************************************************/
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/file.h>

#include <json.h>                          /* We require the json-c library */

#include <cmqec.h>
#include <cmqstrc.h>
#include <cmqcfc.h>
#define MQZEP hc->MQZEP_Call

#if !defined(MQCMDL_LEVEL_930)
#error This module requires a queue manager at minimum of MQ 9.3.0
#endif

#if !defined(TRUE)
#define TRUE (1)
#define FALSE (0)
#endif

/****************************************************************************/
/* This is the completion code that allows the chain to continue if further */
/* services are configured.                                                 */
/****************************************************************************/
#define OA_DEF_CC (MQCC_WARNING)

/****************************************************************************/
/* Declare the internal functions that implement the pieces of the interface*/
/* that we are interested in.                                               */
/* The "_2" versions are needed for the MQZED structure to be passed. They  */
/* are automatically selected when we specify the SupportedVersion value    */
/* during initialisation.                                                   */
/****************************************************************************/
MQZ_INIT_AUTHORITY           MQStart;
static MQZ_TERM_AUTHORITY           OATermAuth;
static MQZ_CHECK_AUTHORITY_2        OACheckAuth;
static MQZ_AUTHENTICATE_USER        OAAuthUser;

static char *trim(char *in,char *out,int l);
static void rpt(char *fmt,...);
static void lock(int);
static void unlock(int);
static char *prettify(char *s,char *p);
static pid_t getThreadId(void);
static void OAEntityStr(json_object *j,PMQZED z);
static json_object *OAAuthStr(json_object *j,MQLONG x);
static char *OACorrelator(char *buf);

#define TMPBUFSIZ  256        /* Should be big enough for any printed values */
#define TIMEFORMAT "%Y-%m-%d %H:%M:%S %Z"
/****************************************************************************/
/* This is where we'll write the logged information. Make sure the          */
/* directory exists in advance of it being needed.                          */
/****************************************************************************/
#if MQAT_DEFAULT == MQAT_UNIX
#define LOGFILE "/var/mqm/audit/oamok.log"
#else
#define LOGFILE "c:\\mqm\\audit\\oamok.log"
#endif

/****************************************************************************/
/* A group  of process-wide global variables.                               */
/****************************************************************************/
static int fd = -1;
static int primary_process = 0;
static MQLONG SupportedVersion = MQZAS_VERSION_6;
static int singleLine = TRUE;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/*************************************************************************/
/* FUNCTION: jcat                                                        */
/* Add a string to a JSON array                                          */
/*************************************************************************/
static void jcat(json_object *j,char *s) {
  json_object_array_add(j,json_object_new_string(s));
}

/*************************************************************************/
/* Function: OAAuthStr                                                   */
/*   Convert the bits in an authorisation request into the string        */
/*   equivalents. Use the abbreviated forms like "allmqi" where possible */
/*************************************************************************/
static json_object *OAAuthStr(json_object *j,MQLONG x)
{
  if ((x & MQZAO_ALL_MQI)== MQZAO_ALL_MQI) {
    jcat(j,"allmqi");
  } else {
    if (x & MQZAO_CONNECT)               jcat(j,"connect");
    if (x & MQZAO_BROWSE )               jcat(j,"browse");
    if (x & MQZAO_INPUT  )               jcat(j,"get");
    if (x & MQZAO_OUTPUT )               jcat(j,"put");
    if (x & MQZAO_INQUIRE)               jcat(j,"inq");
    if (x & MQZAO_SET    )               jcat(j,"set");
    if (x & MQZAO_PUBLISH)               jcat(j,"pub");
    if (x & MQZAO_SUBSCRIBE)             jcat(j,"sub");
    if (x & MQZAO_RESUME)                jcat(j,"resume");

    if (x & MQZAO_PASS_IDENTITY_CONTEXT) jcat(j,"passid");
    if (x & MQZAO_PASS_ALL_CONTEXT)      jcat(j,"passall");
    if (x & MQZAO_SET_IDENTITY_CONTEXT)  jcat(j,"setid");
    if (x & MQZAO_SET_ALL_CONTEXT)       jcat(j,"setall");
    if (x & MQZAO_ALTERNATE_USER_AUTHORITY) jcat(j,"altusr");
  }

  if ((x & MQZAO_ALL_ADMIN) == MQZAO_ALL_ADMIN) {
    jcat(j,"alladm");
  } else {
    if (x & MQZAO_DELETE)                jcat(j,"dlt");
    if (x & MQZAO_DISPLAY)               jcat(j,"dsp");
    if (x & MQZAO_CHANGE)                jcat(j,"chg");
    if (x & MQZAO_CLEAR)                 jcat(j,"clr");

    if (x & MQZAO_CONTROL)               jcat(j,"ctrl");
    if (x & MQZAO_CONTROL_EXTENDED)      jcat(j,"ctrlx");
    if (x & MQZAO_AUTHORIZE)             jcat(j,"auth");
  }

  if (x & MQZAO_CREATE) jcat(j,"crt");
  if (x & MQZAO_SYSTEM) jcat(j,"system");
  if (x & MQZAO_REMOVE) jcat(j,"remove");
  if (x == MQZAO_NONE)  jcat(j,"none");

  return j;
}

/*******************************************************************************/
/* Format the entity name in a structured format - needed on Windows where     */
/* user and domain information are provided.                                   */
/*******************************************************************************/
static void OAEntityStr(json_object *j, PMQZED z)
{
  char buf[TMPBUFSIZ] = {0};

#if MQAT_DEFAULT == MQAT_UNIX
  json_object_object_add(j,"identity",json_object_new_string(trim(z->EntityNamePtr,buf,MQ_USER_ID_LENGTH)));
#else
  json_object_object_add(j,"identity",json_object_new_string(z->EntityNamePtr));
  json_object_object_add(j,"domain"  ,json_object_new_string(z->EntityDomainPtr));
#endif
  return;
}

/*******************************************************************************/
/* All operations for an allocated hConn will occur on the same process/thread */
/* in the queue manager. So we use that pid.tid relationship to generate a     */
/* correlator. All actions for the same connection have the same correlator.   */
/* The correlator might be reused after an application disconnects; you can    */
/* spot the boundary by either an authenticate request or an authorisation for */
/* "connect" auth to the qmgr                                                  */
/*******************************************************************************/
static char *OACorrelator(char *buf) {
  sprintf(buf,"%u.%u",getpid(),getThreadId());
  return buf;
}

/****************************************************************************/
/* Now we get to the functions which are registered to the qmgr.            */
/****************************************************************************/

/****************************************************************************/
/* Function: OACheckAuth                                                    */
/*                                                                          */
/* Description:                                                             */
/*   Called whenever the qmgr wants to see if someone can do something.     */
/*   Typically this will be during MQCONN, MQOPEN or MQSUB. Remember, we're */
/*   never called during MQPUT/MQGET.                                       */
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

  int flags = singleLine?JSON_C_TO_STRING_PLAIN:JSON_C_TO_STRING_PRETTY;
  json_object *jArr;
  json_object *jRoot = json_object_new_object();

  char buf[TMPBUFSIZ] = {0};

  int objLen;
  time_t     now;
  struct tm  ts;
  char       tb[80];

  /* Don't try to log activities that have already failed */
  if (*pCompCode == MQCC_FAILED || *pReason != MQRC_NONE)
  {
    return;
  }

  /* Get current time & format it */
  time(&now);
  localtime_r(&now,&ts);
  strftime(tb, sizeof(tb), TIMEFORMAT, &ts);

  switch (ObjectType)
  {
  case MQOT_CHANNEL:
  case MQOT_SENDER_CHANNEL:
  case MQOT_SERVER_CHANNEL:
  case MQOT_REQUESTER_CHANNEL:
  case MQOT_RECEIVER_CHANNEL:
  case MQOT_SVRCONN_CHANNEL:
  case MQOT_CLNTCONN_CHANNEL:
  case MQOT_TT_CHANNEL:
  case MQOT_AMQP_CHANNEL:
    objLen = MQ_CHANNEL_NAME_LENGTH;
    break;
  default:
    objLen = MQ_Q_MGR_NAME_LENGTH; /* All the others are the same */
    break;
  }

  json_object_object_add(jRoot,"action", json_object_new_string("authorise"));
  json_object_object_add(jRoot,"timeEpoch", json_object_new_int64(now));
  json_object_object_add(jRoot,"timeString", json_object_new_string(tb));
  json_object_object_add(jRoot,"queueManager", json_object_new_string(trim(pQMgrName,buf,MQ_Q_MGR_NAME_LENGTH)));

  json_object_object_add(jRoot,"objectType", json_object_new_string(prettify(MQOT_STR(ObjectType),buf)));
  json_object_object_add(jRoot,"objectName", json_object_new_string(trim(pObjectName,buf,objLen)));

  OAEntityStr(jRoot,pEntityData);

  sprintf(buf,"0x%08X",Authority);
  json_object_object_add(jRoot,"authorityHex", json_object_new_string(buf));

  jArr = json_object_new_array();
  json_object_object_add(jRoot,"authorityString", OAAuthStr(jArr,Authority));
  json_object_object_add(jRoot,"connCorrel", json_object_new_string(OACorrelator(buf)));

  rpt("%s",json_object_to_json_string_ext(jRoot,flags));
  json_object_put(jRoot); /* This frees all the allocated blocks recursively */

  *pCompCode = OA_DEF_CC;
  *pContinuation = MQZCI_CONTINUE;
  return;
}

/****************************************************************************/
/* Function: OAAuthUser                                                     */
/*                                                                          */
/* Description:                                                             */
/*   Called during the connection of any application.                       */
/*                                                                          */
/*   When a clntconn/svrconn channel connects to the queue manager, the     */
/*   authentication is supposed to take two stages. First as the            */
/*   channel program connects, and then as the MCAUSER is set. You will     */
/*   see this as "initial" and "change" context values in the parameters.   */
/****************************************************************************/
static void MQENTRY OAAuthUser (
     PMQCHAR  pQMgrName,
     PMQCSP   pSecurityParms,
     PMQZAC   pApplicationContext,
     PMQZIC   pIdentityContext,
     PMQPTR   pCorrelationPtr,
     PMQBYTE  pComponentData,
     PMQLONG  pContinuation,
     PMQLONG  pCompCode,
     PMQLONG  pReason)
{
  int flags = singleLine?JSON_C_TO_STRING_PLAIN:JSON_C_TO_STRING_PRETTY;
  char buf[TMPBUFSIZ] = {0};

  json_object *jRoot = json_object_new_object();

  time_t     now;
  struct tm  ts;
  char       tb[80];

  char *userId;
  char *authType;
  int userIdLen;

  /* Don't try to log activities that have already failed */
  if (*pCompCode == MQCC_FAILED || *pReason != MQRC_NONE)
  {
    return;
  }

  /* Get current time & format it */
  time(&now);
  localtime_r(&now,&ts);
  strftime(tb, sizeof(tb), TIMEFORMAT, &ts);

  if (pSecurityParms != NULL)
  {
    authType  = (pSecurityParms->AuthenticationType == MQCSP_AUTH_NONE)?"none":"password";
    userId    = pSecurityParms->CSPUserIdPtr;
    userIdLen = pSecurityParms->CSPUserIdLength;
  }
  else
  {
    userId = "N/A";
    authType = "N/A";
    userIdLen=strlen(userId);
  }

  json_object_object_add(jRoot,"action",     json_object_new_string("authenticate"));
  json_object_object_add(jRoot,"timeEpoch",  json_object_new_int64(now));
  json_object_object_add(jRoot,"timeString", json_object_new_string(tb));
  json_object_object_add(jRoot,"queueManager", json_object_new_string(trim(pQMgrName,buf,MQ_Q_MGR_NAME_LENGTH)));

  json_object_object_add(jRoot,"identity",
      json_object_new_string(trim(pIdentityContext->UserIdentifier,buf,MQ_USER_ID_LENGTH)));
  json_object_object_add(jRoot,"applicationName",
      json_object_new_string(trim(pApplicationContext->ApplName,buf,MQ_APPL_NAME_LENGTH)));
  json_object_object_add(jRoot,"environment",
      json_object_new_string(prettify(MQXE_STR(pApplicationContext->Environment),buf)));
  json_object_object_add(jRoot,"caller",
      json_object_new_string(prettify(MQXACT_STR(pApplicationContext->CallerType),buf)));

  json_object_object_add(jRoot,"cspAuthenticationType",  json_object_new_string(authType));
  json_object_object_add(jRoot,"cspUserId",  json_object_new_string(trim(userId,buf,userIdLen)));
  json_object_object_add(jRoot,"authenticationContext",
      json_object_new_string(prettify(MQZAT_STR(pApplicationContext->AuthenticationType),buf)));
  json_object_object_add(jRoot,"bindType",
      json_object_new_string(prettify(MQCNO_STR(pApplicationContext->BindType),buf)));
  json_object_object_add(jRoot,"applicationPid", json_object_new_int(pApplicationContext->ProcessId));
  json_object_object_add(jRoot,"applicationTid", json_object_new_int(pApplicationContext->ThreadId));
  json_object_object_add(jRoot,"connCorrel", json_object_new_string(OACorrelator(buf)));

  rpt("%s",json_object_to_json_string_ext(jRoot,flags));
  json_object_put(jRoot); /* This frees all the allocated blocks recursively */

  *pCompCode = OA_DEF_CC;
  *pReason   = MQRC_NONE;
  *pContinuation = MQZCI_CONTINUE;

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
    if (fd != -1)
    {
      close(fd);
      fd = -1;
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

  *Version   = SupportedVersion;

  /**************************************************************************/
  /* Only 1 process (amqzxma0) gets this per qmgr but that process may      */
  /* still also get a 2ary init later so we don't have an 'else' clause -   */
  /* the default has already been set at the top of the file.               */
  /**************************************************************************/
  if (Options == 0)
  {
    primary_process = 1;
  }

  if (fd == -1)
  {
    /* Don't use buffered I/O (eg fopen) so we can more reliably use locking */
    fd = open(LOGFILE,O_CREAT|O_APPEND|O_WRONLY,0660);
    if (fd == -1)
    {
      CC = MQCC_FAILED;
    }
  }

  /************************************************************************/
  /* An environment variable switches JSON output between single and      */
  /* multi-line output. Single-line is default.                           */
  /************************************************************************/
  if (getenv("AMQ_OAMAUDIT_MULTILINE"))
  {
    singleLine = FALSE;
  }
  else
  {
    singleLine = TRUE;
  }

  /************************************************************************/
  /* Initialise the entry point vectors.  This is performed for both      */
  /* global and process initialisation, ie whatever the value of the      */
  /* Options field. Only a small number of the possible functions         */
  /* need to be registered for this module.                               */
  /************************************************************************/
  if (CC == MQCC_OK)
    MQZEP(hc, MQZID_INIT_AUTHORITY,(PMQFUNC)MQStart,&CC,&Reason);
  if (CC == MQCC_OK)
    MQZEP(hc,MQZID_TERM_AUTHORITY,(PMQFUNC)OATerm,&CC,&Reason);
  if (CC == MQCC_OK)
    MQZEP(hc,MQZID_CHECK_AUTHORITY,(PMQFUNC)OACheckAuth,&CC,&Reason);
  if (CC == MQCC_OK)
    MQZEP(hc,MQZID_AUTHENTICATE_USER,(PMQFUNC)OAAuthUser,&CC,&Reason);

  if ((fd == -1) || (CC != MQCC_OK))
  {
    CC       = MQCC_FAILED;
    Reason   = MQRC_INITIALIZATION_FAILED;
  }

  /**************************************************************************/
  /* And set the return codes.                                              */
  /**************************************************************************/
  *pCompCode = CC;
  *pReason   = Reason;

  return;
}

/****************************************************************************/
/* Remove trailing spaces from a string. We take a copy of the input        */
/* so that the calling queue manager code doesn't see it change. We need to */
/* be given an output buffer of suitable size (l+1).                        */
/****************************************************************************/
static char *trim(char *in,char *out,int l)
{
  int i;
  memset(out,0,TMPBUFSIZ);
  strncpy(out,in,l);

  for (i=strlen(out)-1;i>=0;i--)
  {
     if (out[i] == ' ')
       out[i] = 0;
     else
       break;
  }
  return out;
}

/*************************************************************************/
/* FUNCTION: prettify                                                    */
/* Take an MQI constant such as "MQCA_QUEUE_NAME" and strip underscores &*/
/* spaces to return "queueName".                                         */
/* This uses null-terminated strings. The "out" buffer has to be large   */
/* enough to handle the constants - we can't modify the input buffer.    */
/*************************************************************************/
static char *prettify(char *in,char *out)
{
  char *c;
  unsigned int i;
  MQBOOL upperNext = FALSE;
  MQBOOL seenUnderscore = FALSE;

  if (!in || !strchr(in,'_')) {
    return in;
  }

  i=0;
  memset(out,0,strlen(in)+1);
  for (c=in;*c;c++) {
    if (seenUnderscore) {
      if (*c != '_') {
        if (upperNext) {
          out[i++] = toupper((unsigned char)*c);
          upperNext = FALSE;
        } else {
          out[i++] = tolower((unsigned char)*c);
        }
      } else {
        upperNext = TRUE;
      }
    } else if (*c == '_') {
      seenUnderscore = TRUE;
    }
  }

  /* Fix up the one relevant constant that looks ugly */
  if (!strcmp(out,"q")) {
    strcpy(out,"queue");
  }

  return out;
}

/****************************************************************************/
/* Function: rpt                                                            */
/* Write activity if the log file is open                                   */
/* Make sure the output does end with '\n'. Use locking to try to avoid     */
/* overlapping output.                                                      */
/****************************************************************************/
static void rpt(char *fmt,...)
{
  va_list va;
  va_start(va,fmt);
  char auditBuf[1024] = {0};
  int l,i;
  if (fd != -1) {
    lock(fd);
    l = vsnprintf(auditBuf,sizeof(auditBuf),fmt,va);
    if (singleLine) {
      for (i=0;i<l-1;i++) {
        if (auditBuf[i] == '\n') {
          auditBuf[i] = ' ';
        }
      }
    }
    lseek(fd,0,SEEK_END);
    write(fd,auditBuf,l);
    if (l>0 && auditBuf[l-1] != '\n')
    {
      write(fd,"\n",1);
    }
    unlock(fd);
  }
  va_end(va);
}

/**************************************************************************************/
/* Functions: lock/unlock                                                             */
/* The fcntl locks are shared between all threads in the process, so we also apply    */
/* a process-wide mutex against the threads. Take that thread mutex first and release */
/* last. This should avoid two threads in any process writing to the log file at the  */
/* same time and interleaving.                                                        */
/* A Windows variant of this module would use different primitives for locking.       */
/**************************************************************************************/
static void fc(int fd, int op) {
  struct flock fl;
  fl.l_type = op;
  fl.l_whence = SEEK_SET;
  fl.l_start = 0;
  fl.l_len = 0;
  fcntl(fd,F_SETLK,&fl);
}
static void lock(int fd)
{
  pthread_mutex_lock(&mutex);
  fc(fd,F_WRLCK);
}
static void unlock(int fd)
{
  fc(fd,F_UNLCK);
  pthread_mutex_unlock(&mutex);
}

/**************************************************************************************/
/* Functions: getThreadId                                                             */
/*   Return something unique for the thread identifier.                               */
/**************************************************************************************/
#ifdef _AIX
static pid_t getThreadId() {
  return (pid_t)pthread_self();
}
#else
extern pid_t gettid();
static pid_t getThreadId() {
  return (pid_t)gettid(); /* On Linux, the TID of the first thread is the same as the PID */
}
#endif
