/*****************************************************************************/
/*                                                                           */
/* Module Name: oamlog.c                                                     */
/*                                                                           */
/* (C) IBM Corporation 2001-2020                                             */
/*                                                                           */
/* AUTHOR: Mark Taylor, IBM Hursley                                          */
/*                                                                           */
/* MS07 - Sample Authorisation Service                                       */
/* MS0P - Events and Statistics                                              */
/*                                                                           */
/* PLEASE NOTE - This code is supplied "AS IS" with no                       */
/*              warranty or liability. It is not part of                     */
/*              any product.                                                 */
/*                                                                           */
/* Description: A sample Authorisation Service for IBM MQ                    */
/*                                                                           */
/*   This module implements the MQ Authorisation Service API, defined        */
/*   in the Programmable System Management book.  I hope it also explains    */
/*   some of what actually happens when the interface is invoked, and        */
/*   expands on the information in that book.                                */
/*                                                                           */
/*   It tracks requests made to a real authorisation component such as the   */
/*   OAM shipped with MQ, writing a log as it goes along.  It does not       */
/*   make any authorisation checks itself.  A log may be useful to debug     */
/*   authorisation problems as you can see exactly what request (in          */
/*   particular, which userid) is being checked by the OAM.  Without the log */
/*   you might have to look at MQ trace files or worse ...                   */
/*                                                                           */
/*   As it is supplied in source format, it could be used as the skeleton    */
/*   for a real authorisation service, to replace the IBM-supplied OAM.      */
/*                                                                           */
/*   See the readme supplied with this SupportPac for compilation,           */
/*   configuration and usage information.                                    */
/*                                                                           */
/*****************************************************************************/

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

extern int getpid(void);

/***********************************************************************/
/* Use the following two lines if compiling on V7.1 or later           */
/* The MQI entry points then appear via pointers passed to the exit    */
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
#include <windows.h>
#endif

/*****************************************************************************/
/* This Authorisation module is intended to be run ONLY as the first in a    */
/* chain of installable services. As such, we want the return code given     */
/* to the queue manager to be one which forces the chain to continue.        */
/*                                                                       d    */
/* Some functions - eg Init, Term and Refresh - will continue the chain      */
/* if a service component returns MQCC_OK but most do not. So I'll define    */
/* the return code which will force the chain to continue. If this module    */
/* were changed into one which did real authorisation checking, then I'd     */
/* change this value to MQCC_OK unless I really did want to return an error. */
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
/* will be done. See also the comment about MQRC values in OACheckAuth.      */
/* Of course, if everything has worked OK and authorisation is granted,      */
/* then MQCC_OK/MQRC_NONE would be the correct response.                     */
/*****************************************************************************/
#define OA_DEF_CC (MQCC_WARNING)

/****************************************************************************/
/* Declare the internal functions that implement the interface              */
/* The "_2" versions are needed for the MQZED structure to be passed. They  */
/* are automatically selected when we specify the SupportedVersion value    */
/* during initialisation.                                                   */
/****************************************************************************/
MQZ_INIT_AUTHORITY           MQStart;
static MQZ_TERM_AUTHORITY           OATermAuth;
static MQZ_DELETE_AUTHORITY         OADeleteAuth;
static MQZ_GET_AUTHORITY_2          OAGetAuth;
static MQZ_GET_EXPLICIT_AUTHORITY_2 OAGetExplicitAuth;
static MQZ_ENUMERATE_AUTHORITY_DATA OAEnumAuth;
static MQZ_SET_AUTHORITY_2          OASetAuth;
static MQZ_COPY_ALL_AUTHORITY       OACopyAllAuth;
static MQZ_CHECK_AUTHORITY_2        OACheckAuth;
static MQZ_AUTHENTICATE_USER        OAAuthUser;
static MQZ_FREE_USER                OAFreeUser;
static MQZ_INQUIRE                  OAInquire;
static MQZ_REFRESH_CACHE            OARefreshCache;

static char *trim(char *s);
static void rpt(char *fmt,...);

/****************************************************************************/
/* This is where we'll write the logged information. Make sure the          */
/* directory exists - in particular on Windows where there is no guaranteed */
/* location.                                                                */
/****************************************************************************/
#if MQAT_DEFAULT == MQAT_UNIX
#define LOGFILE "/var/mqm/audit/oamlog.log"
#else
#define LOGFILE "c:\\mqm\\audit\\oamlog.log"
#endif

/****************************************************************************/
/* A group  of process-wide global variables.                               */
/****************************************************************************/
FILE *fp = NULL;

int primary_process = 0;
static MQLONG SupportedVersion = 0;

/****************************************************************************/
/* Some utility functions/macros to dump information.                       */
/****************************************************************************/

/*
 * Note the incredibly inefficient way we do a getpid/gettid on
 * every function call ... This ought to be done with a per-thread
 * setup into a thread-specific area but then the code gets tied to a platform
 * that way (eg pthreads, thread-local-storage, etc).
 *
 * ctime returns a 26 character buffer, of which the final two
 * bytes are '\n\0' so we explicitly print 24 characters from it.
 */
#define TIMEBUF 26
#define PRS "%d.%d @ %24.24s"

#if MQAT_DEFAULT == MQAT_UNIX
static char *v_ctime(char *tb)
{
  time_t t = time(NULL);
  return ctime_r(&t,tb);
}
extern pid_t gettid();
#define prefix(tb) getpid(),gettid(),v_ctime(tb)
#else
static char *v_ctime(char *tb)
{
	time_t t = time(NULL);
	return ctime(&t); /* ought to use a thread-safe version */
}
#define prefix(tb) GetCurrentProcessId(),GetCurrentThreadId(),v_ctime(tb)
#endif

/*
 * Print the Entity Type (user or group or unknown type)
 */
#define OAETStr(x) \
  (x==MQZAET_PRINCIPAL?"User ":(x==MQZAET_GROUP?"Group":"Any"))

/*
 * Decode the Options fields for Init/Term
 */
#define OATermOptStr(x) \
  ((x==MQZTO_SECONDARY)?"Secondary":"Primary")
#define OAInitOptStr(x) \
  ((x==MQZIO_SECONDARY)?"Secondary":"Primary")

/*
 * Print the Entity Name - for Windows we will want to
 * also dump the domain. (The SID might be useful, but it's a
 * slightly more complex format to dump so I've left it out!)
 * I've also put the formatting string in the macro definition so it can
 * be used in the 'real' format string for printing the username.
 *
 * It seems that sometimes the entity name includes trailing spaces
 * and sometimes it doesn't. But at least it is always NULL-terminated.
 * The trim() function will remove the trailing spaces.
 *
 */
#if MQAT_DEFAULT == MQAT_UNIX
#define OAES "\"%s\""
#define OAEntityStr(x) (trim(((x)->EntityNamePtr)))
#else
#define OAES "\"%s@%s\""
#define OAEntityStr(x) \
 (trim(((x)->EntityNamePtr))), \
 ((x)->EntityDomainPtr)?(trim((x)->EntityDomainPtr)):"No Domain"
#endif

/*
 * Print the Object Type
 */
static char *OAOTStr(MQLONG x)
{
  /*
   * There are other defined MQOT_* values, but they should never get
   * sent to the OAM.
   */
  switch (x)
  {
  case 0:              return "Any"; /* Used by dmpmqaut filter */
  case MQOT_Q:         return "Queue";
  case MQOT_NAMELIST:  return "NameList";
  case MQOT_PROCESS:   return "Process";
  case MQOT_Q_MGR:     return "QMgr";
  case MQOT_AUTH_INFO: return "AuthInfo";

  /* Queue subtypes */
  case MQOT_ALIAS_Q:   return "Alias Queue";
  case MQOT_MODEL_Q:   return "Model Queue";
  case MQOT_LOCAL_Q:   return "Local Queue";
  case MQOT_REMOTE_Q:  return "Remote Queue";

  /* More types from V6 */
  case MQOT_LISTENER:  return "Listener";
  case MQOT_SERVICE:   return "Service";
  case MQOT_CHANNEL:   return "Channel";

  /* Channel subtypes */
  case MQOT_SENDER_CHANNEL:     return "Channel Sender";
  case MQOT_SERVER_CHANNEL:     return "Channel Server";
  case MQOT_REQUESTER_CHANNEL:  return "Channel Requester";
  case MQOT_RECEIVER_CHANNEL:   return "Channel Receiver";
  case MQOT_SVRCONN_CHANNEL:    return "Channel SvrConn";
  case MQOT_CLNTCONN_CHANNEL:   return "Channel ClientConn";

  /* More types from V7 */
#ifdef MQOT_TOPIC
  case MQOT_TOPIC: return "Topic";
#endif

  /* Added in V7.1 */
#ifdef MQOT_COMM_INFO
  case MQOT_COMM_INFO:       return "Comm Info";
  case MQOT_CHLAUTH:         return "Channel Auth";
  case MQOT_REMOTE_Q_MGR_NAME: return "Remote QMgr";
#endif

  default:             return "Invalid Object Type";
  }
}

/*
 * This is the Environment value that's available for authentication.
 * It is the same as defined for API Exits.
 */
static char *OAEnvStr(MQLONG x)
{
  switch (x)
  {
  case MQXE_OTHER:          return "Application";
  case MQXE_MCA:            return "Channel";
  case MQXE_MCA_SVRCONN:    return "Channel SvrConn";
  case MQXE_COMMAND_SERVER: return "Command Server";
  case MQXE_MQSC:           return "MQSC";
  default:                  return "Invalid Environment";
  }
}

/*
 * Binding type for the application as it authenticates.
 */
static char *OABTStr(MQLONG x)
{
  switch (x)
  {
  case MQCNO_STANDARD_BINDING: return "Standard";
  case MQCNO_FASTPATH_BINDING: return "Fastpath";
  case MQCNO_SHARED_BINDING:   return "Shared";
  case MQCNO_ISOLATED_BINDING: return "Isolated";
  default:                     return "Invalid BindType";
  }
}

#define OACTStr(x) \
  (x==MQXACT_INTERNAL?"Internal":(x==MQXACT_EXTERNAL?"External":"Invalid Caller Type"))
#define OAATStr(x) \
  (x==MQZAT_INITIAL_CONTEXT?"Initial Context":(x==MQZAT_CHANGE_CONTEXT?"Change Context":"Invalid Auth Type"))

/*
 * Format the requested authorities. This function needs to put stuff into
 * a 'static' buffer so I've set it up to be passed the buffer from the
 * caller. That way we don't need to worry about multi-threaded calling
 * of the module. The buffer needs to be big enough to hold a complete
 * set of permissions; there is no error checking.
 *
 * This may not be the most efficient way of doing it, but I'm more
 * interested in readability than performance.
 *
 * It'll return a string looking something like
 *   0x00000024 [get set ]
 *
 * There are other defined MQZAO_* values, but they should never get
 * sent to the OAM. The setmqaut command knows about the 'allmqi' and 'alladm'
 * group of operations, and to save space I'll print that instead of
 * the individual operations if all bits are set.
 *
 */
#define AUTHBUF 128
#define unknownauthfmt "\tUnk Flag: 0x%08X\n"

static char *OAAuthStr(MQLONG x,char *buf, int *unknownFlags)
{
	int  notAllFlags  = ~(MQZAO_CREATE | MQZAO_REMOVE | MQZAO_ALL);


  sprintf(buf,"0x%08X [",x);

  if ((x & MQZAO_ALL_MQI)== MQZAO_ALL_MQI)
    strcat(buf,"allmqi ");
  else
  {

    if (x & MQZAO_CONNECT)               strcat(buf,"connect ");
    if (x & MQZAO_BROWSE )               strcat(buf,"browse ");
    if (x & MQZAO_INPUT  )               strcat(buf,"get ");
    if (x & MQZAO_OUTPUT )               strcat(buf,"put ");
    if (x & MQZAO_INQUIRE)               strcat(buf,"inq ");
    if (x & MQZAO_SET    )               strcat(buf,"set ");
#ifdef MQZAO_PUBLISH
    if (x & MQZAO_PUBLISH)               strcat(buf,"pub ");
    if (x & MQZAO_SUBSCRIBE)             strcat(buf,"sub ");
    if (x & MQZAO_RESUME)                strcat(buf,"resume ");
#endif

    if (x & MQZAO_PASS_IDENTITY_CONTEXT) strcat(buf,"passid ");
    if (x & MQZAO_PASS_ALL_CONTEXT)      strcat(buf,"passall ");
    if (x & MQZAO_SET_IDENTITY_CONTEXT)  strcat(buf,"setid ");
    if (x & MQZAO_SET_ALL_CONTEXT)       strcat(buf,"setall ");
    if (x & MQZAO_ALTERNATE_USER_AUTHORITY) strcat(buf,"altusr ");
  }

  if (x & MQZAO_CREATE)                strcat(buf,"crt ");

  if ((x & MQZAO_ALL_ADMIN) == MQZAO_ALL_ADMIN)
    strcat(buf,"alladm");
  else
  {
    if (x & MQZAO_DELETE)                strcat(buf,"dlt ");
    if (x & MQZAO_DISPLAY)               strcat(buf,"dsp ");
    if (x & MQZAO_CHANGE)                strcat(buf,"chg ");
    if (x & MQZAO_CLEAR)                 strcat(buf,"clr ");

    /* Added for WMQ V6 */
    if (x & MQZAO_CONTROL)               strcat(buf,"ctrl ");
    if (x & MQZAO_CONTROL_EXTENDED)      strcat(buf,"ctrlx ");
    if (x & MQZAO_AUTHORIZE)             strcat(buf,"auth ");
  }

  /* Added for WMQ V7.0.1 - not part of "alladm" collection */
#ifdef MQZAO_SYSTEM
   if (x & MQZAO_SYSTEM)               strcat(buf,"system ");
#endif

  if (x & MQZAO_REMOVE)                strcat(buf,"rem ");

  if (x == MQZAO_NONE)                 strcat(buf,"none ");

  /*
   * See if there are any unrecognised flags set.
   * Can't easily format the unknown flags because we are
   * already in the middle of an fprintf, and that seems
   * to be (on some platforms at least) non-nestable. Runtime
   * dumps happened if I tried to use the commented code.
   *
   * Other number-formatting functions such as itoa are not
   * available everywhere.
   *
   * Instead, we return the unknown flags and let the
   * caller decide whether to format them.
   */
  *unknownFlags = notAllFlags & x;
  if (*unknownFlags != 0)
  {
#if 0
    char unkbuf[64] = {0};
    sprintf(unkbuf,"unknown (0x%08X) ",unknownFlags);
    strcat(buf,unkbuf);
#else
    strcat(buf,"unknown ");
#endif
  }

  strcat(buf,"]");
  return buf;
}

/*
 * Options used by dmpmqaut. They tell the OAM how to
 * interpret the entity and object/profile names passed as part
 * of the filter.
 */
static char *OAEnumOptStr(MQLONG x,char *buf)
{
  sprintf(buf,"0x%08X [",x);
  if (x == 0)                 strcat(buf,"none ");
  else
  {
   if (x & MQAUTHOPT_CUMULATIVE)             strcat(buf,"cum ");
   if (x & MQAUTHOPT_ENTITY_EXPLICIT)        strcat(buf,"ent_explicit ");
   if (x & MQAUTHOPT_ENTITY_SET)             strcat(buf,"ent_set ");
   if (x & MQAUTHOPT_NAME_ALL_MATCHING)      strcat(buf,"name_all ");
   if (x & MQAUTHOPT_NAME_AS_WILDCARD)       strcat(buf,"name_wildcard ");
   if (x & MQAUTHOPT_NAME_EXPLICIT)          strcat(buf,"name_explicit ");
  }
  strcat(buf,"]");
  return buf;
}

/*
 * Format a pointer. From WMQ V6 lots of the platforms are 64-bit
 * so we have to decide how wide to print. Don't like the default
 * printing style for "%p"...
 */
static char *OAPtrStr(MQPTR p, char *buf)
{
  if (sizeof(MQPTR) == 8)
    sprintf(buf,"0x%016X",p);
  else
    sprintf(buf,"0x%08X",p);
  return buf;
}

/*
 * Attributes that might be inquired upon to see what
 * capabilities the components can support
 */
static char *OAAttrStr(MQLONG x)
{
  switch (x)
  {
  case MQIACF_USER_ID_SUPPORT:   return "UserId Support";
  case MQIACF_INTERFACE_VERSION: return "Interface Version";
  default:                       return "Unknown attribute";
  }
}


/****************************************************************************/
/* Now we get to the real functions which are registered to the qmgr.       */
/****************************************************************************/

/****************************************************************************/
/* Function: OATerm                                                         */
/*                                                                          */
/* Description:                                                             */
/*   The function is invoked during various termination stages (look at     */
/*   the Options field for PRIMARY/SECONDARY flags).                        */
/*                                                                          */
/*   SECONDARY termination is called in current qmgrs only when the         */
/*   process - amqzlaa0 or fastbound application - is about to end. There   */
/*   is no relationship between init/term and MQCONN/MQDISC operations as   */
/*   agents (amqzlaa0 processes) might have multiple threads. We don't get  */
/*   to see threads ending here.                                            */
/*                                                                          */
/*   PRIMARY termination is called once and only once when                  */
/*   the queue manager ends (and comes after all the SECONDARY terminations */
/*   - it's the reverse of init), and hence we're not going to close the    */
/*   logfile in this process until the final termination.                   */
/****************************************************************************/
static char *termfmt = \
  "[" PRS "] OATerm\n"\
  "\tQMgr    : \"%48.48s\"\n"\
  "\tOpts    : 0x%08X [%s]\n"\
  ;

static void MQENTRY OATerm(
  MQHCONFIG  hc,
  MQLONG     Options,
  PMQCHAR    pQMgrName,
  PMQBYTE    pComponentData,
  PMQLONG    pCompCode,
  PMQLONG    pReason)
{
  char tb[TIMEBUF];

  rpt(termfmt,prefix(tb),pQMgrName,Options,OATermOptStr(Options));

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
/* Function: OADeleteAuth                                                   */
/*                                                                          */
/* Description:                                                             */
/*   Remove the authority for all users of an object - the actual object    */
/*   is about to be deleted. It's possible that a tempdyn queue may already */
/*   have been deleted, and this is a just-to-make-sure thing done during   */
/*   restart so a real OAM should perhaps return 'OK' even if the ACLs      */
/*   are no longer in its configuration.                                    */
/****************************************************************************/
static char *delfmt = \
  "[" PRS "] OADeleteAuth\n"\
  "\tObject  : \"%48.48s\" [%s]\n"\
  ;

static void MQENTRY OADeleteAuth(
  PMQCHAR  pQMgrName,
  PMQCHAR  pObjectName,
  MQLONG   ObjectType,
  PMQBYTE  pComponentData,
  PMQLONG  pContinuation,
  PMQLONG  pCompCode,
  PMQLONG  pReason)
{
  char tb[TIMEBUF];
  rpt(delfmt,
      prefix(tb),
	   pObjectName,
	   OAOTStr(ObjectType));
  *pCompCode = OA_DEF_CC;
  *pReason   = MQRC_NONE;
  *pContinuation = MQZCI_CONTINUE;
  return;
}

/****************************************************************************/
/* Function: OARefreshCache                                                 */
/*                                                                          */
/* Description:                                                             */
/*   Available from MQSeries V5.2, this tells the OAM to refresh            */
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
  char tb[TIMEBUF];
  rpt("[" PRS "] OARefreshCache\n", prefix(tb));
  *pCompCode = MQCC_OK;
  *pReason   = MQRC_NONE;
  *pContinuation = MQZCI_CONTINUE;
  return;
}

/****************************************************************************/
/* Function: OAGetAuth                                                      */
/*                                                                          */
/* Description:                                                             */
/*   Called to return the current Authority assigned to this user/group.    */
/*   Because the Authority is a value returned from the OAM later in the    */
/*   chain, we can't access it here.                                        */
/*                                                                          */
/*   The value returned from the IBM-shipped OAM would be the union of the  */
/*   authorities assigned to all groups to which the user belongs.          */
/****************************************************************************/
static char *getauthfmt = \
  "[" PRS "] OAGetAuth\n"\
  "\tObject  : \"%48.48s\" [%s]\n"\
  "\t%-6.6s  : " OAES "\n"\
  ;

static void MQENTRY OAGetAuth(
  PMQCHAR  pQMgrName,
  PMQZED   pEntityData,
  MQLONG   EntityType,
  PMQCHAR  pObjectName,
  MQLONG   ObjectType,
  PMQLONG  pAuthority,
  PMQBYTE  pComponentData,
  PMQLONG  pContinuation,
  PMQLONG  pCompCode,
  PMQLONG  pReason)
{
  char tb[TIMEBUF];
  rpt(getauthfmt,
	  prefix(tb),
	  pObjectName,
	  OAOTStr(ObjectType),
	  OAETStr(EntityType),
	  OAEntityStr(pEntityData));
  *pCompCode = OA_DEF_CC;
  *pReason   = MQRC_NONE;
  *pContinuation = MQZCI_CONTINUE;
  return;
}

/****************************************************************************/
/* Function: OAGetExplicitAuth                                              */
/*                                                                          */
/* Description:                                                             */
/*   This is called to return the Authority assigned to a particular user   */
/*   or group. What's the difference between Get and GetExplicit?           */
/*   The answer (in the IBM-shipped OAM) is whether or not secondary groups */
/*   are considered. This is used during creation of new object, where we   */
/*   want to find the authority of the creating user. If you're writing     */
/*   your own OAM, then you might want to make the two Get functions do the */
/*   same thing.                                                            */
/*                                                                          */
/****************************************************************************/
static char *explauthfmt = \
  "[" PRS "] OAGetExplicitAuth\n"  \
  "\tObject  : \"%48.48s\" [%s]\n" \
  "\t%-6.6s  : " OAES "\n"\
  ;

static void MQENTRY OAGetExplicitAuth(
  PMQCHAR  pQMgrName,
  PMQZED   pEntityData,
  MQLONG   EntityType,
  PMQCHAR  pObjectName,
  MQLONG   ObjectType,
  PMQLONG  pAuthority,
  PMQBYTE  pComponentData,
  PMQLONG  pContinuation,
  PMQLONG  pCompCode,
  PMQLONG  pReason)
{
  char tb[TIMEBUF];
  rpt(explauthfmt,
	  prefix(tb),
	  pObjectName,
	  OAOTStr(ObjectType),
	  OAETStr(EntityType),
	  OAEntityStr(pEntityData));

  *pCompCode = OA_DEF_CC;
  *pReason   = MQRC_NONE;
  *pContinuation = MQZCI_CONTINUE;
  return;
}

/****************************************************************************/
/* Function: OASetAuth                                                      */
/*                                                                          */
/* Description:                                                             */
/*   Sets the authority for a user to the object. Called whenever you run   */
/*   setmqaut and also when objects are created.                            */
/****************************************************************************/

static char *setauthfmt = \
  "[" PRS "] OASetAuth\n"\
  "\tObject  : \"%48.48s\" [%s]\n"\
  "\t%-6.6s  : " OAES "\n"\
  "\tAuth    : %s\n"\
  ;

static void MQENTRY OASetAuth(
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
  char buf[AUTHBUF] = {0};
  char tb[TIMEBUF];
  int  unknownAuthFlags = 0;

  if (fp)
  {
  	fprintf(fp,setauthfmt,
	    prefix(tb),
	    pObjectName,
	    OAOTStr(ObjectType),
	    OAETStr(EntityType),
	    OAEntityStr(pEntityData),
	    OAAuthStr(Authority,buf,&unknownAuthFlags));
    if (unknownAuthFlags != 0)
    	fprintf(fp,unknownauthfmt,unknownAuthFlags);
  }

  *pCompCode = OA_DEF_CC;
  *pReason   = MQRC_NONE;
  *pContinuation = MQZCI_CONTINUE;
  return;
}

/****************************************************************************/
/* Function: OACopyAllAuth                                                  */
/*                                                                          */
/* Description:                                                             */
/*   Sets the authority for a new object, based on a reference set of       */
/*   permissions. Obviously both the reference and the new object have      */
/*   the same object type. In fact, it's only called (today) during         */
/*   creation of a dynamic queue. Unfortunately there's nothing in this     */
/*   interface which says whether or not it's a temporary or permanent      */
/*   dynamic queue, which could be useful to know, as TDQs expire along     */
/*   with the qmgr and info about them need not be hardened.                */
/****************************************************************************/
static char *copyallfmt = \
  "[" PRS "] OACopyAllAuth\n"\
  "\tFrom    : \"%48.48s\" [%s]\n"\
  "\tTo      : \"%48.48s\"\n"\
  ;

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
  char tb[TIMEBUF];
  rpt(copyallfmt,
	  prefix(tb),
	  pRefObjectName,
	  OAOTStr(ObjectType),
	  pObjectName);
  *pCompCode = OA_DEF_CC;
  *pReason   = MQRC_NONE;
  *pContinuation = MQZCI_CONTINUE;
  return;
}

/****************************************************************************/
/* Function: OACheckAuth                                                    */
/*                                                                          */
/* Description:                                                             */
/*   Called whenever the qmgr wants to see if someone can do something.     */
/*   Typically this will be during MQCONN, MQOPEN or MQSUB. Remember, we're */
/*   never called during MQPUT/MQGET.                                       */
/*                                                                          */
/*   The qmgr code will break out of the chaining loop whatever the CC      */
/*   and whatever the Continuation parameters say,                          */
/*   unless some very specific error codes happen. This is so that          */
/*   one, and only one, service returns a response granting access to the   */
/*   object. But for this particular service we want the chain always to    */
/*   continue. So I set a combination of Reason/CC/Continuation which will  */
/*   pass the qmgr checks and then move on to the real OAM.                 */
/*                                                                          */
/*   A proper authorisation component would do some real work here! And     */
/*   then set MQCC_OK (or FAILED) and some appropriate error code.          */
/****************************************************************************/
static char *checkauthfmt = \
  "[" PRS "] OACheckAuth\n" \
  "\tObject  : \"%48.48s\" [%s]\n" \
  "\t%-6.6s  : " OAES "\n" \
  "\tAuth    : %s\n"\
  ;

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
  char buf[AUTHBUF] = {0};
  char tb[TIMEBUF];
  int  unknownAuthFlags = 0;
  if (fp)
  {
  	fprintf(fp,checkauthfmt,
	    prefix(tb),
	    pObjectName,
	    OAOTStr(ObjectType),
	    OAETStr(EntityType),
	    OAEntityStr(pEntityData),
	    OAAuthStr(Authority,buf,&unknownAuthFlags));
    if (unknownAuthFlags != 0)
    	fprintf(fp,unknownauthfmt,unknownAuthFlags);
  }

  *pCompCode = OA_DEF_CC;
  *pReason   = MQRC_UNKNOWN_OBJECT_NAME;
  *pContinuation = MQZCI_CONTINUE;
  return;
}


#ifdef MQZID_AUTHENTICATE_USER
/****************************************************************************/
/* Function: OAAuthUser                                                     */
/*                                                                          */
/* Description:                                                             */
/*   Called during the connection of any application. This allows the OAM   */
/*   to change the userid associated with the connection, regardless of the */
/*   operating system userid. One reason you might want to do that is to    */
/*   deal with non-standard userids, which perhaps are longer than 12       */
/*   characters. The CorrelationPtr can be assigned in this function to     */
/*   point to some OAM-managed storage, and is available as part of the     */
/*   MQZED structure for all subsequent functions. Note that there is only  */
/*   one CorrelPtr stored for the user's hconn, so if two OAMs are chained  */
/*   and both want to manage storage for the connection, there would be     */
/*   difficulties as there is no reverse call that would allow the second   */
/*   to reset the first's pointer (or vice versa). I'd suggest instead      */
/*   using something like thread-specific storage as each thread is tied    */
/*   to the hconn.                                                          */
/*                                                                          */
/*   If you are using that correlation technique, then you MIGHT want to    */
/*   store the "real" userid in shared storage, but you probably don't need */
/*   to as all operations for this particular connection will be handled    */
/*   on this thread. So a simple malloc/free ought to suffice.              */
/*                                                                          */
/*   Lots of context information is provided to assist in the authentication*/
/*   decision; this function prints out what appear to be the most useful.  */
/*                                                                          */
/*   When a clntconn/svrconn channel connects to the queue manager, the     */
/*   authentication is supposed to take two stages. First as the            */
/*   channel program connects, and then as the MCAUSER is set. You will     */
/*   see this as "initial" and "change" context values in the parameters.   */
/****************************************************************************/
static char *authuserfmt =
  "[" PRS "] OAAuthUser\n"\
  "\tUser    : \"%12.12s\"\n"\
  "\tEffUser : \"%12.12s\"\n"\
  "\tAppName : \"%28.28s\"\n"\
  "\tApIdDt  : \"%32.32s\"\n"\
  "\tEnv     : \"%s\"\n"\
  "\tCaller  : \"%s\"\n"\
  "\tType    : \"%s\"\n"\
  "\tBind    : \"%s\"\n"\
  "\tApp Pid : %d\n"\
  "\tApp Tid : %d\n"\
  "\tPtr     : %s\n"\
  ;

static void MQENTRY OAAuthUser (
     PMQCHAR  pQMgrName,
     PMQCSP   pSecurityParms,
     PMQZAC   pApplicationContext,
     PMQZIC   pIdentityContext,
     PMQPTR   pCorrelationPtr,   /* cmqzc.h declares as MQPTR, ought to be PMQPTR   */
     PMQBYTE  pComponentData,
     PMQLONG  pContinuation,
     PMQLONG  pCompCode,
     PMQLONG  pReason)
{
  char buf[32] = {0};
  char tb[TIMEBUF];

  rpt(authuserfmt,
	  prefix(tb),
	  pIdentityContext->UserIdentifier,
	  pApplicationContext->EffectiveUserID,
	  pApplicationContext->ApplName,
	  pIdentityContext->ApplIdentityData,
	  OAEnvStr(pApplicationContext->Environment),
	  OACTStr(pApplicationContext->CallerType),
	  OAATStr(pApplicationContext->AuthenticationType),
	  OABTStr(pApplicationContext->BindType),
	  pApplicationContext->ProcessId,
	  pApplicationContext->ThreadId,
	  OAPtrStr(*pCorrelationPtr,buf)
	  );

  *pCompCode = OA_DEF_CC;
  *pReason   = MQRC_NONE;
  *pContinuation = MQZCI_CONTINUE;
  return;
}

/****************************************************************************/
/* Function:  OAFreeUser                                                    */
/*                                                                          */
/* Description:                                                             */
/*   Called during MQDISC, as the inverse of the Authenticate. If the OAM   */
/*   has allocated private storage to hold additional information about     */
/*   the user, then this is the time to free it. No more calls will be made */
/*   to the OAM for this connection instance of this user.                  */
/*                                                                          */
/*  This is ONLY called if one of the services in the chain has a set a     */
/*  non-NULL correlation pointer, so you cannot necessarily rely on it being*/
/*  called to simply indicate all MQDISCs.                                  */
/****************************************************************************/
static char *freeuserfmt = \
  "[" PRS "] OAFreeUser\n"
  "\tPtr     : %s\n"\
  ;

static void MQENTRY OAFreeUser (
     PMQCHAR  pQMgrName,
     PMQZFP   pFreeParms,
     PMQBYTE  pComponentData,
     PMQLONG  pContinuation,

     PMQLONG  pCompCode,
     PMQLONG  pReason)
{
  char buf[32] = {0};
  char tb[TIMEBUF];

  rpt(freeuserfmt,
    prefix(tb),
    OAPtrStr(pFreeParms->CorrelationPtr,buf));

  *pCompCode = OA_DEF_CC;
  *pReason   = MQRC_NONE;
  *pContinuation = MQZCI_CONTINUE;
  return;
}

/****************************************************************************/
/* Function: OAInquire                                                      */
/*                                                                          */
/* Description:                                                             */
/*   Unlike all the other functions in the Installable Service definition   */
/*   this one is invoked for all components in turn, regardless of the      */
/*   return codes. It requires that the component return its capabilities.  */
/*   Currently the only two capabilities requested are the version of       */
/*   interface that is supported, and an indication of whether the module   */
/*   understands userids as well as groups. These are both integer attrs    */
/*   so the CharAttr parameters can be ignored. (They are there for         */
/*   consistency with the other attribute setting/getting interfaces.)      */
/*                                                                          */
/*   Only a single response is returned to the program invoking this        */
/*   function; it is, in effect, the summation of the capabilities of       */
/*   all the chained OAMs.                                                  */
/*                                                                          */
/*   The components are called in reverse-order of their definition. If     */
/*   this module was defined as the first Auth Service in the qm.ini file,  */
/*   then it can print out the values returned from the real OAM.           */
/*   It could also override - to extend or restrict - operations that get   */
/*   sent to later OAMs in the chain.                                       */
/*                                                                          */
/*   This was added to WMQ V6 as part of the PCF-enablement of security     */
/*   functions, and to assist a single administration tool that could       */
/*   work with both Unix and Windows authorities.                           */
/****************************************************************************/
static char *attrfmt = \
"\tAttr    : \"%-20.20s\" [%4d]  Value : %d\n";

static void MQENTRY OAInquire( MQCHAR48 QMgrName
	                   , MQLONG   SelectorCount
	                   , PMQLONG  pSelectors
	                   , MQLONG   IntAttrCount
	                   , PMQLONG  pIntAttrs
	                   , MQLONG   CharAttrLength
	                   , PMQCHAR  pCharAttrs
	                   , PMQLONG  pSelectorsReturned
	                   , PMQBYTE  ComponentData
	                   , PMQLONG  pContinuation
	                   , PMQLONG  pCompCode
	                   , PMQLONG  pReason
	                   )
{
  MQLONG  attr;
  MQLONG  val;
  MQLONG  i;

  char tb[TIMEBUF];

  rpt("[" PRS "] OAInquire\n", prefix(tb));

  if( SelectorCount == 0)
  {
    rpt("\tNo selectors\n");
  }
  else
  {
    for(i=0;i<SelectorCount;i++)
    {
      attr = pSelectors[i];
      val  = pIntAttrs[i];
      rpt(attrfmt,
	      OAAttrStr(attr),
	      attr,
	      val);
    }
  }

  *pCompCode = MQCC_OK;
  *pReason   = MQRC_NONE;
  *pContinuation = MQZCI_CONTINUE;

  return;
}

#endif

#ifdef MQZID_ENUMERATE_AUTHORITY_DATA
/****************************************************************************/
/* Function: OAEnumAuth                                                     */
/*                                                                          */
/* Description:                                                             */
/*   This function is called when the dmpmqaut command extracts all of the  */
/*   authority settings for a queue manager. The pFilter structure points   */
/*   to the fields that can select by userid, or by object etc. When no     */
/*   extraneous parameters are given to dmpmqaut, the fields are empty or   */
/*   zeroed, as appropriate.                                                */
/*                                                                          */
/*   The StartEnumeration is set for the first call to the OAM, and cleared */
/*   for subsequent calls, until all the matching data has been returned.   */
/*                                                                          */
/*   The options field tells the OAM how to interpret the filter fields. In */
/*   particular it will say whether or not a string including a wildcard    */
/*   should look for an entry that IS that wildcard or whether it           */
/*   SATISFIES the regular expression.                                      */
/*                                                                          */
/*   The filter may not contain any entity information, so we should not    */
/*   try to format it unless there really is something.                     */
/****************************************************************************/
static char *authenumfmt = \
  "[" PRS "] OAEnumAuth\n"\
  "\tStart   : %s\n"\
  "\tOptions : %s\n"\
  "\tProfile : \"%48.48s\" [%s] \n"\
  "\tEntType : %s\n"
  "\tEntity  : " OAES "\n"\
  "\tAuth    : %s\n"\
  ;

static void MQENTRY OAEnumAuth(
     PMQCHAR  pQMgrName,
     MQLONG   StartEnumeration,
     PMQZAD   pFilter,
     MQLONG   AuthorityBufferLength,
     PMQZAD   pAuthorityBuffer,
     PMQLONG  pAuthorityDataLength,
     PMQBYTE  pComponentData,
     PMQLONG  pContinuation,
     PMQLONG  pCompCode,
     PMQLONG  pReason)
{

  char buf[128] = {0};
  char buf2[AUTHBUF] = {0};
  char tb[TIMEBUF];
  int unknownAuthFlags=0;

  int decodeEnt = 0;

  if (pFilter->EntityType == MQZAET_PRINCIPAL ||
      pFilter->EntityType == MQZAET_GROUP)
      decodeEnt = 1;

  if (fp)
  {
  	fprintf(fp, authenumfmt,
	    prefix(tb),
  	  ((StartEnumeration == 0)?"No":"Yes"),
	    OAEnumOptStr(pFilter->Options,buf),
  	    pFilter->ProfileName,
	    OAOTStr(pFilter->ObjectType),
	    OAETStr(pFilter->EntityType),
        (decodeEnt!=0) ? OAEntityStr(pFilter->EntityDataPtr): "Not Specified",
	    OAAuthStr(pFilter->Authority,buf2,&unknownAuthFlags)
	  );
    if (unknownAuthFlags != 0)
    	fprintf(fp,unknownauthfmt,unknownAuthFlags);
  }
  *pCompCode = OA_DEF_CC;
  *pReason   = MQRC_NONE;
  *pContinuation = MQZCI_CONTINUE;

  return;
}

#endif

#ifdef MQZID_CHECK_PRIVILEGED
/****************************************************************************/
/* CheckPrivileged is new with MQ V7.1 - given an id, is it considered an   */
/* MQ administrator or not. For example, is the id in the 'mqm' group.      */
/****************************************************************************/
static char *checkprivfmt = \
  "[" PRS "] OACheckPriv\n"\
  "\tEntType : %s\n"
  "\tEntity  : " OAES "\n"\
  ;
static void MQENTRY OACheckPrivileged( MQCHAR48 QMgrName,
		PMQZED pEntityData,
		MQLONG EntityType,
		PMQBYTE  pComponentData,
		PMQLONG  pContinuation,
		PMQLONG  pCompCode,
		PMQLONG  pReason)
{
  char buf[128] = {0};
  char buf2[AUTHBUF] = {0};
  char tb[TIMEBUF];
	 int decodeEnt = 0;

	  if (EntityType == MQZAET_PRINCIPAL ||
	      EntityType == MQZAET_GROUP)
	      decodeEnt = 1;

	  if (fp)
	  {
	  	fprintf(fp, checkprivfmt,
		    prefix(tb),
		    OAETStr(EntityType),
	        (decodeEnt!=0) ? OAEntityStr(pEntityData): "Not Specified"
		  );

	  }

	*pCompCode = OA_DEF_CC;
	*pReason   = MQRC_NONE;
	*pContinuation = MQZCI_CONTINUE;
	return;
}
#endif

/****************************************************************************/
/* Function: MQStart                                                        */
/*                                                                          */
/* Description:                                                             */
/*   This is the initialisation and entrypoint for the dynamically loaded   */
/*   authorisation installable service. It registers the addresses of the   */
/*   other functions which are to be called by the qmgr.                    */
/*                                                                          */
/*   This function is called whenever the module is loaded.  The Options    */
/*   field will show whether it's a PRIMARY (ie during qmgr startup) or     */
/*   SECONDARY (any other time - normally during the start of an agent      */
/*   process which is not necessarily the same as during MQCONN, especially */
/*   when running multi-threaded agents) initialisation, but there's        */
/*   nothing different that we'd want to do here based on that flag.        */
/*                                                                          */
/*   Because of when the init function is called, there is no need to       */
/*   worry about multi-threaded stuff in this particular function, and      */
/*   hence no need to do something like a lock around the fopen() call.     */
/*                                                                          */
/* ComponentDataSize and ComponentData :                                    */
/*                                                                          */
/*   We do not use ComponentData at all in this module so the               */
/*   ComponentDataSize should be set to zero. If it's                       */
/*   non-zero, then a block of data is allocated from a chunk of            */
/*   shared memory and available for all processes running this module      */
/*   within this queue manager. It could be used to hold truly global       */
/*   state info, but then you'd also need to manage potential simultaneous  */
/*   updates to it.                                                         */
/*                                                                          */
/* Note:                                                                    */
/*   This function MUST be called MQStart on some platforms, so we'll use   */
/*   the same name for all of them.                                         */
/****************************************************************************/
static char *initfmt = \
  "[" PRS "] OAInit\n"\
  "\tQMgr    : \"%48.48s\"\n"\
  "\tCC      : %d  "\
  "\tRC      : %d\n"\
  "\tCompSize: %d\n"\
  "\tOptions : 0x%08X [%s]\n"\
  ;

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
  char tb[TIMEBUF];

  /**************************************************************************/
  /* Only 1 process (amqzxma0) gets this per qmgr but that process may      */
  /* still also get a 2ary init later so we don't have an 'else' clause -   */
  /* the default has already been set at the top of the file.               */
  /**************************************************************************/
  if (Options == 0)
    primary_process = 1;

  if (!fp)
  {
    fp = fopen(LOGFILE,"a");
    if (fp)
    {
      setbuf(fp,NULL);    /* try to reduce interleaved output; auto-flush */
    }
    else
    {
	    CC = MQCC_FAILED;
    }
  }

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
    MQZEP(hc,MQZID_DELETE_AUTHORITY,(PMQFUNC)OADeleteAuth,&CC,&Reason);

  if (CC == MQCC_OK)
    MQZEP(hc,MQZID_SET_AUTHORITY,(PMQFUNC)OASetAuth,&CC,&Reason);

  if (CC == MQCC_OK)
    MQZEP(hc,MQZID_GET_AUTHORITY,(PMQFUNC)OAGetAuth,&CC,&Reason);

  if (CC == MQCC_OK)
    MQZEP(hc,MQZID_GET_EXPLICIT_AUTHORITY,(PMQFUNC)OAGetExplicitAuth,&CC,&Reason);

#ifdef MQZID_REFRESH_CACHE
  /************************************************************************/
  /* This is defined from MQ V5.2 systems. Don't try to compile this code */
  /* on a 5.2 machine and then run it on 5.1.                             */
  /************************************************************************/
  if (CC == MQCC_OK)
    MQZEP(hc,MQZID_REFRESH_CACHE,(PMQFUNC)OARefreshCache,&CC,&Reason);
#endif

#ifdef MQZID_ENUMERATE_AUTHORITY_DATA
  /************************************************************************/
  /* Introduced in WMQ V5.3 as part of the support for generic            */
  /* profiles. This dumps the information stored by an OAM.               */
  /************************************************************************/
  if (CC == MQCC_OK)
    MQZEP(hc,MQZID_ENUMERATE_AUTHORITY_DATA,(PMQFUNC)OAEnumAuth,&CC,&Reason);
#endif

#ifdef MQZID_AUTHENTICATE_USER
  /************************************************************************/
  /* These were introduced in WMQ V6.                                     */
  /************************************************************************/
  if (CC == MQCC_OK)
    MQZEP(hc,MQZID_AUTHENTICATE_USER,(PMQFUNC)OAAuthUser,&CC,&Reason);
  if (CC == MQCC_OK)
    MQZEP(hc,MQZID_FREE_USER,(PMQFUNC)OAFreeUser,&CC,&Reason);
  if (CC == MQCC_OK)
    MQZEP(hc,MQZID_INQUIRE,(PMQFUNC)OAInquire,&CC,&Reason);
#endif

#ifdef MQZID_CHECK_PRIVILEGED
  if (CC == MQCC_OK)
  	MQZEP(hc,MQZID_CHECK_PRIVILEGED,(PMQFUNC)OACheckPrivileged,&CC,&Reason);
#endif
    rpt(initfmt,
	    prefix(tb),
	    QMgrName,
	    CC,
	    Reason,
	    ComponentDataLength,
	    Options,
	    OAInitOptStr(Options));


  if ((!fp) || (CC != MQCC_OK))
  {
    CC       = MQCC_FAILED;
    Reason   = MQRC_INITIALIZATION_FAILED;
  }


  /**************************************************************************/
  /* There have been several versions of the Auth Service interface.        */
  /* We will tell the qmgr to call us with the V2 parameters, which are     */
  /* needed on Windows to get the SID and domain name information.          */
  /* If MQZID_REFRESH_CACHE value is defined, then we can do the V3 version.*/
  /* And V5 (which was introduced with WMQ V6) is needed to look at the     */
  /* authentication interfaces. WMQ V7 has no new interface functions but   */
  /* there were extensions to the object types and permissions to support   */
  /* the Publish/Subscribe capabilities.                                    */
  /**************************************************************************/
#ifdef MQOT_REMOTE_Q_MGR
  SupportedVersion   = MQZAS_VERSION_6; /* For WMQ V7.1 */
#elif MQZID_AUTHENTICATE_USER
  SupportedVersion   = MQZAS_VERSION_5;
#elif MQZID_ENUMERATE_AUTHORITY_DATA
  SupportedVersion   = MQZAS_VERSION_4;
#elif MQZID_REFRESH_CACHE
  SupportedVersion   = MQZAS_VERSION_3;
#else
  SupportedVersion   = MQZAS_VERSION_2;
#endif
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
static void rpt(char *fmt,...)
{
  va_list va;
  va_start(va,fmt);
  int l;
  if (fp) {
    vfprintf(fp,fmt,va);
    l = strlen(fmt);
    if (l>0 && fmt[l-1] != '\n')
    {
      fprintf(fp,"\n");
    }
  }
  va_end(va);
}
