#if !defined(_H_INCLUDE_MQIOTEL)
#define _H_INCLUDE_MQIOTEL

#include <cmqc.h>
#include <cmqxc.h>

typedef void MQENTRY MQ_OPEN_AND_SUB_EXIT (
   PMQAXP    pExitParms,  
   PMQAXC    pExitContext, 
   PMQCHAR   verb,
   PMQHCONN  pHconn,        
   PPMQOD    ppObjDesc, 
   PMQLONG   pOptions,    
   PPMQHOBJ  ppHobj,      /* The queue or sub handle */  
   PPMQHOBJ  ppHManagedObj,  /* The managed queue from MQSUB */
   PMQLONG   pCompCode, 
   PMQLONG   pReason); 
typedef MQ_OPEN_AND_SUB_EXIT MQPOINTER PMQ_OPEN_AND_SUB_EXIT;

#endif
