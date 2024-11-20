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

// Use this as a bitmap filter to pull out relevant value from GMO.
// The AS_Q_DEF value is 0 so would not contribute.
#define GETPROPSOPTIONS MQGMO_PROPERTIES_FORCE_MQRFH2 | MQGMO_PROPERTIES_IN_HANDLE | MQGMO_NO_PROPERTIES | MQGMO_PROPERTIES_COMPATIBILITY

static MQLONG gmoLength(PMQGMO gmo) {
  switch (gmo->Version) {
  case MQGMO_VERSION_1:
    return MQGMO_LENGTH_1;
  case MQGMO_VERSION_2:
    return MQGMO_LENGTH_2;
  case MQGMO_VERSION_3:
    return MQGMO_LENGTH_3;
  case MQGMO_VERSION_4:
  default:
    return MQGMO_LENGTH_4;
  }
}

static phobjOptions saveGmo(PMQHCONN hc, PMQHOBJ ho, PMQGMO gmo) {
  phobjOptions o;
  string key = objectKey(hc, ho);
  if (objectOptionsMap.count(key) == 0) {
    o = (hobjOptions *)mqotMalloc(sizeof(hobjOptions));
    objectOptionsMap[key] = o;
  } else {
    o = objectOptionsMap[key];
  }
  o->gmo = gmo;
  return o;
}

static PMQGMO restoreGmo(PMQHCONN hc, PMQHOBJ ho) {
  string key = objectKey(hc, ho);
  return objectOptionsMap[key]->gmo;
}

string extractRFH2PropVal(const char *propsC, int l, const char *prop) {
  string props = string(propsC, l);

  // rpt("Props: %s Prop:%s",props.c_str(),prop);
  string val = "";
  string propXml = "<" + string(prop) + ">";
  size_t start = props.find(propXml);
  if (start != string::npos) {
    // Where does the next tag begin
    size_t end = props.find("<", start + propXml.length());
    if (end != string::npos) {

      val = props.substr(start + propXml.length(), end - start - propXml.length());
    }
  }
  return val;
}

vector<string> split(const string &str, const string &delim) {
  vector<string> tokens;
  size_t prev = 0, pos = 0;
  do {
    pos = str.find(delim, prev);
    if (pos == string::npos)
      pos = str.length();
    string token = str.substr(prev, pos - prev);
    if (!token.empty())
      tokens.push_back(token);
    prev = pos + delim.length();
  } while (pos < str.length() && prev < str.length());
  return tokens;
}

static void HexToBinary(const string hex, uint8_t *buf, const int buflen) {
  // printf("Converting %s: ",hex.c_str());
  for (size_t i = 0; i * 2 < hex.length() && i < buflen; i++) {
    string byteString = hex.substr(i * 2, 2);

    uint8_t byteValue = static_cast<uint8_t>(stoi(byteString, nullptr, 16));
    // printf("%02X",byteValue);
    buf[i] = byteValue;
  }
  // printf("\n");
  return;
}

// Create an empty attributes map, used when linking the inbound to the active span
static const opentelemetry::common::KeyValueIterableView<std::array<std::pair<std::string, int>, 0>> &GetEmptyAttributes() noexcept {
  static const std::array<std::pair<std::string, int>, 0> array{};
  static const opentelemetry::common::KeyValueIterableView<std::array<std::pair<std::string, int>, 0>> kEmptyAttributes(array);

  return kEmptyAttributes;
}

extern "C" {
MQ_GET_EXIT mqotGetBefore;
MQ_GET_EXIT mqotGetAfter;

void mqotGetBefore(PMQAXP pExitParms, PMQAXC pExitContext, PMQHCONN pHconn, PMQHOBJ pHobj, PPMQMD ppMsgDesc, PPMQGMO ppGetMsgOpts, PMQLONG pBufferLength,
                   PPMQVOID ppBuffer, PPMQLONG ppDataLength, PMQLONG pCompCode, PMQLONG pReason) {

  MQLONG propCtl;
  PMQGMO gmo = *ppGetMsgOpts;

  // Option combinations:
  // MQGMO_NO_PROPERTIES: Always add our own handle
  // MQGMO_PROPERTIES_IN_HANDLE: Use it
  // MQGMO_PROPERTIES_COMPAT/FORCE_RFH2: Any returned properties will be in RFH2
  // MQGMO_PROPERTIES_AS_Q_DEF:
  //      PROPCTL: NONE: same as GMO_NO_PROPERTIES
  //               ALL/COMPAT/V6COMPAT: Any returned properties will be either in RFH2 or Handle if supplied
  //               FORCE: Any returned properties will be in RFH2
  //                "unknown": Can't guess - may or may not be OK

  MQLONG propGetOptions = gmo->Options & GETPROPSOPTIONS;

  if (gmo->Version >= MQGMO_VERSION_4 && isValidHandle(gmo->MsgHandle)) {
    rpt("Using app-supplied msg handle");
  } else {
    auto o = saveGmo(pHconn, pHobj, gmo);

    string key = objectKey(pHconn, pHobj);
    propCtl = -1;
    if (objectOptionsMap.count(key) == 1) {
      auto opts = objectOptionsMap[key];
      propCtl = opts->propCtl;

      // Stash a copy of the original GMO and build a new one that
      // is guaranteed to be at least Version4 length (to recognise handles)
      PMQGMO myGmo = &o->myGmo;
      o->myGmo = {MQGMO_DEFAULT};
      memcpy(myGmo, gmo, gmoLength(gmo));

      if (myGmo->Version < MQGMO_VERSION_4) {
        myGmo->Version = MQGMO_VERSION_4;
      }

      // Make the real MQGET use our GMO instead of the app-supplied version
      *ppGetMsgOpts = myGmo;

      // If we know that the app or queue is configured for not returning any properties, then we will override that into our handle
      if ((propGetOptions == MQGMO_NO_PROPERTIES) || (propGetOptions == MQGMO_PROPERTIES_AS_Q_DEF && propCtl == MQPROP_NONE)) {
        gmo->Options &= ~MQGMO_NO_PROPERTIES;
        gmo->Options |= MQGMO_PROPERTIES_IN_HANDLE;

        myGmo->MsgHandle = getMsgHandle(pExitParms, pHconn, pHobj);
        rpt("Using mqiotel msg handle. getPropsOptions=%d propCtl=%d\n", propGetOptions, propCtl);
      } else {
        // Hopefully they will have set something suitable on the PROPCTL attribute
        // or are asking specifically for an RFH2-style response
        rpt("Not setting a message handle. propGetOptions=%08X\n", propGetOptions);
      }
    }

    return;
  }
}

// Extract the properties from the message, either with the properties API
// or from the RFH2. Construct a new context with the span information from the inbound message and
// if there's an existing SpanContext, add a link to the original
// We do not try to extract/propagate any baggage-related fields.
void mqotGetAfter(PMQAXP pExitParms, PMQAXC pExitContext, PMQHCONN pHconn, PMQHOBJ pHobj, PPMQMD ppMsgDesc, PPMQGMO ppGetMsgOpts, PMQLONG pBufferLength,
                  PPMQVOID ppBuffer, PPMQLONG ppDataLength, PMQLONG pCompCode, PMQLONG pReason) {

  PMQGMO gmo = *ppGetMsgOpts;
  PMQMD md = *ppMsgDesc;
  MQLONG CC, RC;
  PMQVOID buffer = *ppBuffer;

  string traceparentVal = "";
  string tracestateVal = "";

  bool haveMsg = true;

  int removed = 0;
  if (*pCompCode != MQCC_OK && *pReason != MQRC_TRUNCATED_MSG_ACCEPTED) {
    haveMsg = false;
  }
  MQHMSG mh = gmo->MsgHandle;
  if (isValidHandle(mh)) {
    if (haveMsg) {
      rpt("Looking for context in handle");

      MQPD pd = {MQPD_DEFAULT};
      MQIMPO impo = {MQIMPO_DEFAULT};

      impo.Options = MQIMPO_CONVERT_VALUE | MQIMPO_INQ_FIRST;

      string val = propsValue(pExitParms, pHconn, mh, TRACEPARENT, &CC, &RC);
      if (CC == MQCC_OK) {
        rpt("Found traceparent property: %s", val.c_str());
        traceparentVal = val;
      } else {
        if (RC != MQRC_PROPERTY_NOT_AVAILABLE) {
          // Should not happen
          rptmqrc("GetAfter (1)", CC, RC);
        }
      }

      val = propsValue(pExitParms, pHconn, mh, TRACESTATE, &CC, &RC);
      if (CC == MQCC_OK) {
        rpt("Found tracestate property: %s", val.c_str());
        tracestateVal = val;
      } else {
        if (RC != MQRC_PROPERTY_NOT_AVAILABLE) {
          // Should not happen
          rptmqrc("GetAfter (2)", CC, RC);
        }
      }
    }

    // If we added our own handle in the GMO, then reset
    // but don't do it for async callbacks. Indicated by the HOBJ value.
    if ((*pHobj == MQHO_UNUSABLE_HOBJ) && compareMsgHandle(pHconn, pHobj, mh)) {
      *ppGetMsgOpts = restoreGmo(pHconn, pHobj);
      rpt("Removing our handle");
    }

    // Should we also remove the properties?
    // Probably not worth it, as any app dealing with
    // properties ought to be able to handle unexpected props.

  } else if (haveMsg && md && !strncmp(md->Format, MQFMT_RF_HEADER_2, MQ_FORMAT_LENGTH)) {
    rpt("Looking for context in RFH2");
    PMQRFH2 rfh2 = (PMQRFH2)buffer;
    MQLONG offset = MQRFH_STRUC_LENGTH_FIXED_2;

    int propsLen = rfh2->StrucLength - offset;
    char *b = (char *)buffer;

    // dumpHex("RFH2", buffer,rfh2->StrucLength);
    // rpt("Offset: %d Len: %d",offset,propsLen);

    // Need to then step past the MQLONG for the NameValueLen part of the RFH2 properties
    // There might be multiple blocks of NameValues, but only the first one is actually relevant
    // for our searching.
    offset += 4;
    propsLen -= 4;

    char *props = &b[offset];

    traceparentVal = extractRFH2PropVal(props, propsLen, TRACEPARENT);
    tracestateVal = extractRFH2PropVal(props, propsLen, TRACESTATE);

    rpt("Found parent:%s state:%s", traceparentVal.c_str(), tracestateVal.c_str());

    /*
    if otelOpts.RemoveRFH2 {
        // If the only properties in the RFH2 are the OTEL ones, then perhaps
        // the application cannot process the message. But we don't know for sure,
        // and maybe the properties are useful for higher-level span generation.
        // So we ought to have an options to forcibly remove the RFH2.
        md.Format = rfh2.Format
        md.CodedCharSetId = rfh2.CodedCharSetId
        md.Encoding = rfh2.Encoding
        reset *pDataLen
        offset = rfh2->StrucLength
         memmove(&buf[0], &buf[offset], datalen - offset);

      }
      }
    */
  } else {
    rpt("No properties or RFH2 found");
  }

  // We now should have the relevant message properties to pass upwards
  auto currentSpan = trace_api::Tracer::GetCurrentSpan();
  if (currentSpan->GetContext().IsValid()) {
    bool haveNewContext = false;

    auto traceId = trace_api::TraceId();
    auto spanId = trace_api::SpanId();
    auto traceFlags = trace_api::TraceFlags();
    auto traceState = trace_api::TraceState::GetDefault();

    if (traceparentVal != "") {
      // Split the inbound traceparent value into its components to allow
      // construction of a new context
      auto elem = split(traceparentVal, "-");
      if (elem.size() == 4) {
        // elem[0] = 0 (version indicator. Always 0 for now)

        uint8_t traceIdBuf[trace_api::TraceId::kSize];
        HexToBinary(elem[1], traceIdBuf, trace_api::TraceId::kSize);
        traceId = trace_api::TraceId{traceIdBuf};

        uint8_t spanIdBuf[trace_api::SpanId::kSize];
        HexToBinary(elem[2], spanIdBuf, trace_api::SpanId::kSize);
        spanId = trace_api::SpanId{spanIdBuf};
        // Final element can only be 00 or 01 (for now)
        if (elem[3] == "01") {
          traceFlags = trace_api::TraceFlags(trace_api::TraceFlags::kIsSampled);
        }
        haveNewContext = true;
      }
    }

    if (tracestateVal != "") {
      // Build a TraceState structure by parsing the string
      traceState = trace_api::TraceState::FromHeader(tracestateVal);
      haveNewContext = true;
    }

    // If there is a current span, and we have at least one of the
    // parent/state properties, then create a link referencing these values
    if (haveNewContext) {
      auto spanContext = trace_api::SpanContext{traceId, spanId, traceFlags, true, traceState};
      // See https://github.com/open-telemetry/opentelemetry-specification/issues/454 for why this only works
      // with ABI V2
#if defined OPENTELEMETRY_ABI_VERSION_NO && OPENTELEMETRY_ABI_VERSION_NO >= 2
      currentSpan->AddLink(spanContext, GetEmptyAttributes());
      rpt("Added link to current span");
#else
      // Allow compilation to continue, because there may be scenarios where you don't need
      // to call the AddLink function. But issue a compiler warning.
      // Also, the application and this exit must be compiled with the same ABI option. Which is
      // checked in the mqotInit method.
#warning "Must use OPENTELEMETRY_ABI_VERSION_NO = 2 to support adding links to inbound messages"
      rpt("Skipping AddLink operation as ABI VERSION %d too low", OPENTELEMETRY_ABI_VERSION_NO);
#endif
    } else {
      rpt("No context properties found");
    }
  } else {
    // If there is no current active span, then we are not going to
    // try to create a new one, as we would have no way of knowing when it
    // ends. The properties are (probably) still available to the application if
    // it wants to work with them itself.
    rpt("No current span to update");
  }

  return;
}

// For the end of "C"
}