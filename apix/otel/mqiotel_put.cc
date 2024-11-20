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

#include <stdarg.h>
#include <stdio.h>

#include <cmqc.h>
#include <cmqec.h>

#include <trace/span.h>
#include <trace/trace_state.h>
#include <trace/tracer.h>


#include "mqiotel.hpp"

namespace trace_api = opentelemetry::trace;

static phobjOptions savePmo(PMQHCONN hc, PMQHOBJ ho, PMQPMO pmo) {
  phobjOptions o;
  string key = objectKey(hc, ho);
  if (objectOptionsMap.count(key) == 0) {
    o = (hobjOptions *)mqotMalloc(sizeof(hobjOptions));
    objectOptionsMap[key] = o;
  } else {
    o = objectOptionsMap[key];
  }
  o->pmo = pmo;
  return o;
}

static PMQPMO restorePmo(PMQHCONN hc, PMQHOBJ ho) {
  string key = objectKey(hc, ho);
  return objectOptionsMap[key]->pmo;
}

static MQLONG pmoLength(PMQPMO pmo) {
  switch (pmo->Version) {
  case MQPMO_VERSION_1:
    return MQPMO_LENGTH_1;
  case MQPMO_VERSION_2:
    return MQPMO_LENGTH_2;
  case MQPMO_VERSION_3:
  default:
    return MQPMO_LENGTH_3;
  }
}

// These functions need to be available via dlsym, so using the "C" directive around them
extern "C" {

MQ_PUT_EXIT mqotPutBefore;
MQ_PUT_EXIT mqotPutAfter;

void mqotPutBefore(PMQAXP pExitParms, PMQAXC pExitContext, PMQHCONN pHconn, PMQHOBJ pHobj, PPMQMD ppMsgDesc, PPMQPMO ppPutMsgOpts, PMQLONG pBufferLength,
                   PPMQVOID ppBuffer, PMQLONG pCompCode, PMQLONG pReason) {
  MQHMSG mh;
  MQHMSG mho;

  PMQPMO pmo = *ppPutMsgOpts;
  PMQMD md = *ppMsgDesc;
  void *buffer = *ppBuffer;

  bool skipParent = false;
  bool skipState = false;

  rpt("In mqotPutBefore\n");

  // Is the app already using a MsgHandle for its PUT? If so, we
  // can piggy-back on that. If not, then we need to use our
  // own handle. That handle can be reused for all PUTs/GETs on this
  // hConn. This works, even when the app is primarily using an RFH2 for
  // its own properties - the RFH2 and the Handle contents are merged.
  //
  // If there was an app-provided handle, then have they set
  // either of the key properties? If so, then we will
  // leave them alone as we are not trying to create a new span in this
  // layer.

  if (pmo->Version >= MQPMO_VERSION_3 && isValidHandle(pmo->NewMsgHandle)) {
    rpt("Using pmo->NewMsgHandle");

    mh = pmo->NewMsgHandle;
    if (propsContain(pExitParms, pHconn, mh, TRACEPARENT)) {
      skipParent = true;
    }
    if (propsContain(pExitParms, pHconn, mh, TRACESTATE)) {
      skipState = true;
    }
  } else if (pmo->Version >= MQPMO_VERSION_3 && isValidHandle(pmo->OriginalMsgHandle)) {
    mh = pmo->OriginalMsgHandle;
    rpt("Using pmo->OriginalMsgHandle");

    if (propsContain(pExitParms, pHconn, mho, TRACEPARENT)) {
      skipParent = true;
    }
    if (propsContain(pExitParms, pHconn, mho, TRACESTATE)) {
      skipState = true;
    }
  } else {
    rpt("Creating my own handle");

    // Stash a copy of the original PMO and build a new one that
    // is guaranteed to be at least Version3 length (to recognise handles)
    auto o = savePmo(pHconn, pHobj, pmo);

    PMQPMO myPmo = &o->myPmo;
    o->myPmo = {MQPMO_DEFAULT};
    memcpy(myPmo, pmo, pmoLength(pmo));

    mh = getMsgHandle(pExitParms, pHconn, NULL);
    myPmo->OriginalMsgHandle = mh;
    if (myPmo->Version < MQPMO_VERSION_3) {
      myPmo->Version = MQPMO_VERSION_3;
    }

    // Make the real MQPUT use our PMO instead of the app-supplied version
    *ppPutMsgOpts = myPmo;
  }

  // The message MIGHT have been constructed with an explicit RFH2
  // header. If so, then we extract the properties
  // from that header (assuming there's only a single structure, and it's not
  // chained). Then very simply look for the property names in there as strings. These tests would
  // incorrectly succeed if someone had put "traceparent" into a non-"usr" folder but that would be
  // very unexpected.
  if (md && !strncmp(md->Format, MQFMT_RF_HEADER_2, 8)) {
    PMQRFH2 hdr = (PMQRFH2)buffer;
    MQLONG offset = MQRFH_STRUC_LENGTH_FIXED_2;

    int propsLen = hdr->StrucLength - offset;
    char *b = (char *)buffer;
    string props = string(&b[offset], propsLen);

    if (props.find("<" + string(TRACEPARENT) + ">", 0)) {
      skipParent = true;
    }
    if (props.find("<" + string(TRACESTATE) + ">", 0)) {
      skipState = true;
    }
  }

  // We're now ready to extract the context information and set the MQ message property
  // We are not going to try to propagate baggage via another property
  auto span = trace_api::Tracer::GetCurrentSpan();
  if (span->GetContext().IsValid()) {
    MQSMPO smpo = {MQSMPO_DEFAULT};
    MQPD pd = {MQPD_DEFAULT};
    MQLONG CC, RC;
    MQCHARV propertyNameVS = {MQCHARV_DEFAULT};
    MQLONG pType = MQTYPE_STRING;

    rpt("About to extract context from an active span");
    if (!skipParent) {
      char trace_id_buf[32];
      char span_id_buf[16];
      char trace_flags_buf[2];
      auto trace_id = span->GetContext().trace_id();
      auto span_id = span->GetContext().span_id();
      auto trace_flags = span->GetContext().trace_flags();

      trace_id.ToLowerBase16(trace_id_buf);
      span_id.ToLowerBase16(span_id_buf);
      trace_flags.ToLowerBase16(trace_flags_buf);

      // This is the W3C-defined format for the trace property
      char value[128];
      sprintf(value, "%s-%32.32s-%16.16s-%2.2s", "00", trace_id_buf, span_id_buf, trace_flags_buf);
      rpt("Setting %s to %s", TRACEPARENT, value);

      propertyNameVS.VSPtr = (PMQVOID)TRACEPARENT;
      propertyNameVS.VSLength = MQVS_NULL_TERMINATED;

      pExitParms->Hconfig->MQSETMP_Call(*pHconn, mh, &smpo, &propertyNameVS, &pd, pType, strlen(value), value, &CC, &RC);
      if (CC != MQCC_OK) {
        rptmqrc("MQSETMP", CC, RC);
      }
    }

    if (!skipState) {
      // Need to convert any traceState map to a single serialised string
      auto ts = span->GetContext().trace_state();
      if (ts) {
        auto value = ts->ToHeader().c_str();
        if (value != NULL && strlen(value) > 0) {
          rpt("Setting %s to \"%s\"", TRACESTATE, value);
          propertyNameVS.VSPtr = (PMQVOID)TRACESTATE;
          propertyNameVS.VSLength = MQVS_NULL_TERMINATED;

          pExitParms->Hconfig->MQSETMP_Call(*pHconn, mh, &smpo, &propertyNameVS, &pd, pType, strlen(value), (void *)value, &CC, &RC);
          if (CC != MQCC_OK) {
            rptmqrc("MQSETMP", CC, RC);
          }
        }
      }
    }
  } else {
    rpt("Cannot find active span");
  }

  return;
}

// If we added our own MsgHandle to the PMO, then remove it
// before returning to the application. We don't need to delete
// the handle as it can be reused for subsequent PUTs on this hConn
void mqotPutAfter(PMQAXP pExitParms, PMQAXC pExitContext, PMQHCONN pHconn, PMQHOBJ pHobj, PPMQMD ppMsgDesc, PPMQPMO ppPutMsgOpts, PMQLONG pBufferLength,
                  PPMQVOID ppBuffer, PMQLONG pCompCode, PMQLONG pReason) {
  PMQPMO pmo = *ppPutMsgOpts;
  MQHMSG mh = pmo->OriginalMsgHandle;
  if (compareMsgHandle(pHconn, NULL, mh)) {
    rpt("Restoring original PMO");
    *ppPutMsgOpts = restorePmo(pHconn, pHobj);
  }

  return;
}
}