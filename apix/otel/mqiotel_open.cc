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

#include <cstring>
#include <stdarg.h>
#include <stdio.h>

#include <cmqc.h>
#include <cmqec.h>

#include "mqiotel.hpp"

using namespace std;

// Options in an MQOPEN that mean we might do MQGET
// Do not include BROWSE variants
#define OPEN_GET_OPTIONS (MQOO_INPUT_AS_Q_DEF | MQOO_INPUT_SHARED | MQOO_INPUT_EXCLUSIVE)

extern "C" {
#include "mqiotel.h"

MQ_OPEN_AND_SUB_EXIT mqotOpenAfter;
MQ_CLOSE_EXIT mqotCloseAfter;

// Get rid of stashed details of the object that's being Closed
void mqotCloseAfter(PMQAXP pExitParms, PMQAXC pExitContext, PMQHCONN pHconn, PPMQHOBJ ppHobj, PMQLONG pOptions, PMQLONG pCompCode, PMQLONG pReason) {

  string key = objectKey(pHconn, *ppHobj);

  // The "ignored" queues do not appear in the regular maps. So we clean them up here.
  ignoreSetLock.lock();
  if (ignoreSet.count(key) == 1) {
    // rpt("CloseAfter: Removing %s",key.c_str());
    ignoreSet.insert(key);
    ignoreSetLock.unlock();
    return;
  }
  ignoreSetLock.unlock();

  rpt("> CloseAfter hObj=%d",**ppHobj);

  optionsMapLock();
  if (objectOptionsMap.count(key) == 1) {
    auto o = objectOptionsMap[key];
    if (o->hObjManaged != MQHO_UNUSABLE_HOBJ) {
      auto managed_key = objectKey(pHconn, &o->hObjManaged);
      if (objectOptionsMap.count(managed_key) == 1) {
         auto managed_o = objectOptionsMap[managed_key];
         rpt("  CloseAfter deleting managed queue %s",managed_key.c_str());
         mqotFree(managed_o);
         objectOptionsMap.erase(managed_key);
      }
    }
    rpt("  CloseAfter deleting key %s",key.c_str());
    mqotFree(o);
    objectOptionsMap.erase(key);
  }
  optionsMapUnlock();

  rpt("< CloseAfter");

  return;
}

// When a queue is opened for INPUT, then it will help to
// know the PROPCTL setting so we know if we can add a MsgHandle or to expect
// an RFH2 response. If the MQINQ fails, that's OK - we'll just ignore the error
// but might not be able to get any property/RFH from an inbound message
//
// Note that we can't (and don't need to) do the same for an MQPUT1 because the
// information we are trying to discover is only useful on MQGET/CallBack.
//
// If the MQOPEN is for a topic (via MQSUB) AND there's a managed object created for the
// target queue, then that will always have the MQOO_INQUIRE setup for us.

void mqotOpenAfter(PMQAXP pExitParms, PMQAXC pExitContext, PMQCHAR verb, PMQHCONN pHconn, PPMQOD ppObjDesc, PMQLONG pOptions,
                   PPMQHOBJ ppHobj, PPMQHOBJ ppHobjManaged, PMQLONG pCompCode, PMQLONG pReason) {
  PMQOD od = *ppObjDesc;

  PMQHOBJ pHobj = *ppHobj;
  PMQHOBJ pHobjManaged = *ppHobjManaged;
  MQLONG propCtl;
  MQLONG openOptions = *pOptions;

  const char *n;
  if (od && od->ObjectType == MQOT_Q) {
    n = od->ObjectName;
  } else {
    n = "<<N/A>>";
  }

  // This queue gets referenced quite often internally when you open a real queue for
  // the application. We stash the hConn/hObj so the MQGETs for it are ignored. If only
  // to keep any tracing output less confusing.
  string key = objectKey(pHconn, pHobj);
  if (!strncmp(n,"SYSTEM.PROTECTION.POLICY.QUEUE", 48)) {
    // rpt("OpenAfter: Adding ignored hObj %s",key.c_str());
    ignoreSetLock.lock();
    ignoreSet.insert(key);
    ignoreSetLock.unlock();

    return;
  }

  rpt("> OpenAfter for %s using object %-48.48s hObj=%d",verb,n, (pHobj?*pHobj:-1));

  // Do the MQINQ and stash the information
  // Only care if there's an INPUT option. We do the MQINQ on every relevant MQOPEN
  // because it might change between an MQCLOSE and a subsequent MQOPEN. The MQCLOSE
  // will, in any case, have discarded the entry from this map.
  // If the user opened the queue with MQOO_INQUIRE, then we can reuse the object handle.
  // Otherwise we have to do our own open/inq/close.
  if ((od && (od->ObjectType == MQOT_Q) && (openOptions & OPEN_GET_OPTIONS) != 0) || (pHobjManaged != NULL)) {
    auto key = objectKey(pHconn, pHobj);
    MQLONG CC, RC;
    propCtl = 0;

    MQLONG selectors[] = {MQIA_PROPERTY_CONTROL};
    MQLONG values[1];

    if ((pHobjManaged == NULL) && (openOptions & MQOO_INQUIRE) != 0) {
      rpt("%s: Reusing existing hObj",verb);
      pExitParms->Hconfig->MQINQ_Call(*pHconn, *pHobj, 1, selectors, 1, values, 0, NULL, &CC, &RC);

      if (CC == MQCC_OK) {
        rpt("Inq Response: %d", values[0]);
        propCtl = values[0];
      } else {
        rptmqrc("open: Inq err", CC, RC);
        propCtl = -1;
      }
    } else if (pHobjManaged)   {
      rpt("open: Using managed hObj");
      pExitParms->Hconfig->MQINQ_Call(*pHconn, *pHobjManaged, 1, selectors, 1, values, 0, NULL, &CC, &RC);
      if (CC == MQCC_OK) {
        rpt("Inq Response: %d", values[0]);
        propCtl = values[0];
      } else {
        rptmqrc("open: Inq err", CC, RC);
        propCtl = -1;
      }
      auto managed_key = objectKey(pHconn, pHobjManaged);
      phobjOptions o = (hobjOptions *)mqotMalloc(sizeof(hobjOptions));
      o->propCtl = propCtl;
      o->hObjManaged = *pHobjManaged;
      optionsMapLock();
      // replace any existing value for this object handle
      rpt("  OpenAfter adding options for managed key %s",managed_key.c_str());

      objectOptionsMap[managed_key] = o;
      optionsMapUnlock();


    } else {
      MQOD inqOd = {MQOD_DEFAULT};
      MQHOBJ inqHobj;

      strncpy(inqOd.ObjectName, od->ObjectName, 48);
      strncpy(inqOd.ObjectQMgrName, od->ObjectQMgrName, 48);
      inqOd.ObjectType = MQOT_Q;
      MQLONG inqOpenOptions = MQOO_INQUIRE;

      rpt("open: pre-reopen");
      // This does not recurse as an API Exit's calls to the MQI are not sent back into the Exit
      pExitParms->Hconfig->MQOPEN_Call(*pHconn, &inqOd, inqOpenOptions, &inqHobj, &CC, &RC);

      if (CC != MQCC_OK) {
        rptmqrc("open: Reopen err",  CC, RC);

        propCtl = -1;
      } else {
        pExitParms->Hconfig->MQINQ_Call(*pHconn, inqHobj, 1, selectors, 1, values, 0, NULL, &CC, &RC);

        if (CC == MQCC_OK) {
          rpt("Inq response: %d", values[0]);
          propCtl = values[0];
        } else {
          rptmqrc("open: Inq err", CC, RC);
          propCtl = -1;
        }

        pExitParms->Hconfig->MQCLOSE_Call(*pHconn, &inqHobj, 0, &CC, &RC); // Ignore any error
      }
    }
    // Create an object to hold the discovered value
    phobjOptions o = (hobjOptions *)mqotMalloc(sizeof(hobjOptions));
    o->propCtl = propCtl;
    if (pHobjManaged) {
      // Stash the managed hObj so we can deal with it when the SUB is CLOSEd
      o->hObjManaged = *pHobjManaged;
    } else {
      o->hObjManaged = MQHO_UNUSABLE_HOBJ;
    }
    // replace any existing value for this object handle
    rpt("  OpenAfter adding options for key %s",key.c_str());
    optionsMapLock();
    objectOptionsMap[key] = o;
    optionsMapUnlock();

  } else {
    rpt("%s: not doing Inquire",verb);
  }

  rpt("< OpenAfter for %s",verb);
  return;
}
}
