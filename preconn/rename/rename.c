/***********************************************************************/
/*                                                                     */
/* Module Name: rename.c                                               */
/* (C) IBM Corporation 2022                                            */
/*                                                                     */
/* AUTHOR: Anthony Beardsmore, IBM Hursley                             */
/*                                                                     */
/* Licensed under the Apache License, Version 2.0 (the "License");     */
/* you may not use this file except in compliance with the License.    */
/* You may obtain a copy of the License at                             */
/*                                                                     */
/*      http://www.apache.org/licenses/LICENSE-2.0                     */
/*                                                                     */
/* Unless required by applicable law or agreed to in writing, software */
/* distributed under the License is distributed on an "AS IS" BASIS,   */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,                       */
/* either express or implied.                                          */
/*                                                                     */
/* See the License for the specific language governing permissions and */
/* limitations under the License.                                      */
/***********************************************************************/
/*                                                                     */
/*  This file provides a very simple sample mq client preconnect exit  */
/*  to 'override' the name for the target QM connected to.             */ 
/*                                                                     */
/*    MQENTRY RenamePreconnectExit ( pExitParms,                       */
/*                               , QmgrName                            */
/*                               , ppConnectOpts                       */
/*                               , pCompCode                           */
/*                               , pReasonCode)                        */
/*                                                                     */
/*                                                                     */
/*  Add PreConnect stanza in mqclient.ini                              */
/*  For Example                                                        */
/*                                                                     */
/*  PreConnect:                                                        */
/*  Module=<Module>                                                    */
/*  Function=RenamePreconnectExit                                      */
/*  Data=*ANY_QM                                                       */
/*  Sequence=1                                                         */
/*                                                                     */
/*  where Module is the path of the compiled shared library/dll        */
/*                                                                     */
/*        Function is the entry point of the PreConnect exit code      */
/*                                                                     */
/*        Data is the parameter string containing a QM Name or         */
/*        generic name to be used for the subsequent MQCONN.  If       */
/*        Data is not supplied or left blank, '*' is assumed.          */
/*                                                                     */
/*        PreConnect exit Sequence is the sequence in which this exit  */
/*        is called relative to other exits.                           */
/***********************************************************************/

#include <cmqc.h>
#include <cmqxc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/*********************************************************************/
/*                                                                   */
/* Function: RenamePreconnectExit                                    */
/*                                                                   */
/* Description: Main entry point for Rename preconnect exit          */
/* Intended Function: Main entry point                               */
/*                                                                   */
/* Input Parameters: pExitParms - Pointer to an MQNXP structure      */
/*                   pQMgrName  - Name of the QMgr to be overridden  */
/*                   ppConnectOpts - MQCNO options. Not used by this */
/*                                exit. Hence remains unmodified.    */
/*                   pCompCode  - MQ Completion code.                */
/*                   pReasonCode - MQ Reason code.                   */
/*                                                                   */
/* Output Parameters: Depends on the Exit Reason.                    */
/*                   MQXR_INIT - N/A                                 */
/*                   MQXR_TERM - N/A                                 */
/*                   MQXR_PRECONNECT - updates QMName parameter      */
/*                                                                   */
/* Returns: void                                                     */
/*********************************************************************/
void MQENTRY RenamePreconnectExit ( PMQNXP  pExitParms
                                  , PMQCHAR pQMgrName
                                  , PPMQCNO ppConnectOpts
                                  , PMQLONG pCompCode
                                  , PMQLONG pReason)
{
  MQLONG rc = MQCC_OK;

  if(pExitParms == NULL)
  {
    *pCompCode = MQXCC_FAILED;
    *pReason = MQRC_API_EXIT_ERROR;
    goto MOD_EXIT;
  }

  /* Initialize ExitResponse and ExitResponse2 */
  pExitParms->ExitResponse = MQXCC_OK;
  pExitParms->ExitResponse2 = MQXR2_DEFAULT_CONTINUATION;

  /* What job are we doing */
  switch(pExitParms->ExitReason)
  {
    case MQXR_INIT:
    {
        /* No-op */
    }
    break;

    case MQXR_TERM:
    {
        /* No-op */
    }
    break;

    case MQXR_PRECONNECT:
    {
      MQCHAR *newName = (MQCHAR *)pExitParms->pExitDataPtr;
      if(newName[0] != '\0')
        /* Use exactly what was provided via mqclient.ini */
        strncpy(pQMgrName, newName, MQ_Q_MGR_NAME_LENGTH);
      else
      {
        /* Default to 'any queue manager' */
        pQMgrName[0] = '*';
        pQMgrName[1] = '\0';
      }
    }
    break;

    default:
    {
       pExitParms->ExitResponse = MQXCC_SUPPRESS_FUNCTION;
      *pCompCode = MQCC_FAILED;
      *pReason = MQRC_API_EXIT_ERROR;
    }
  }

MOD_EXIT:
  return;
}

