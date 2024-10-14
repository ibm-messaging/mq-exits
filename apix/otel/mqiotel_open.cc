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
MQ_OPEN_EXIT mqotOpenAfter;
MQ_CLOSE_EXIT mqotCloseAfter;

// Get rid of stashed details of the object that's being Closed
void mqotCloseAfter(PMQAXP pExitParms, PMQAXC pExitContext, PMQHCONN pHconn, PPMQHOBJ ppHobj, PMQLONG pOptions, PMQLONG pCompCode, PMQLONG pReason) {

  auto key = objectKey(pHconn, *ppHobj);
  if (objectOptionsMap.count(key) == 1) {
    auto o = objectOptionsMap[key];
    mqotFree(o);
    objectOptionsMap.erase(key);
  }

  return;
}

// When a queue is opened for INPUT, then it will help to
// know the PROPCTL setting so we know if we can add a MsgHandle or to expect
// an RFH2 response. If the MQINQ fails, that's OK - we'll just ignore the error
// but might not be able to get any property/RFH from an inbound message
//
// Note that we can't (and don't need to) do the same for an MQPUT1 because the
// information we are trying to discover is only useful on MQGET/CallBack.
void mqotOpenAfter(PMQAXP pExitParms, PMQAXC pExitContext, PMQHCONN pHconn, PPMQOD ppObjDesc, PMQLONG pOptions, PPMQHOBJ ppHobj, PMQLONG pCompCode,
                   PMQLONG pReason) {
  PMQOD od = *ppObjDesc;

  PMQHOBJ pHobj = *ppHobj;
  MQLONG propCtl;
  MQLONG openOptions = *pOptions;
  // Do the MQINQ and stash the information
  // Only care if there's an INPUT option. We do the MQINQ on every relevant MQOPEN
  // because it might change between an MQCLOSE and a subsequent MQOPEN. The MQCLOSE
  // will, in any case, have discarded the entry from this map.
  // If the user opened the queue with MQOO_INQUIRE, then we can reuse the object handle.
  // Otherwise we have to do our own open/inq/close.
  if ((od->ObjectType == MQOT_Q) && (openOptions & OPEN_GET_OPTIONS) != 0) {
    auto key = objectKey(pHconn, pHobj);
    MQLONG CC, RC;
    propCtl = 0;

    MQLONG selectors[] = {MQIA_PROPERTY_CONTROL};
    MQLONG values[1];

    if ((openOptions & MQOO_INQUIRE) != 0) {
      rpt("open: Reusing existing hObj");
      pExitParms->Hconfig->MQINQ_Call(*pHconn, *pHobj, 1, selectors, 1, values, 0, NULL, &CC, &RC);

      if (CC == MQCC_OK) {
        rpt("Inq Response: %d", values[0]);
        propCtl = values[0];
      } else {
        rptmqrc("open: Inq err", CC, RC);
        propCtl = -1;
      }
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
        rptmqrc("open: Reopen err", CC, RC);
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
    // replace any existing value for this object handle
    objectOptionsMap[key] = o;

  } else {
    rpt("open: not doing Inquire");
  }

  return;
}
}