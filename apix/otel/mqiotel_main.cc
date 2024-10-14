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

#include <plugin/detail/loader_info.h>
#include <version.h>

#include "mqiotel.hpp"

using namespace std;

// What level of the C++ ABI do we need
#define REQUIRED_ABI 2 // For the AddLink() function on Spans

map<string, MQHMSG> objectHandleMap;
map<string, phobjOptions> objectOptionsMap;

bool initialised = false;

// Logger function in parent
RPT_FN *rptMain = NULL;

bool isValidHandle(MQHMSG mh) {
  bool rc = false;
  if (mh != MQHM_NONE && mh != MQHM_UNUSABLE_HMSG) {
    rc = true;
  }
  return rc;
}

// Is there a property of the given name?
string propsValue(PMQAXP pExitParms, PMQHCONN pHconn, MQHMSG mh, const char *propertyName, PMQLONG pCC, PMQLONG pRC) {

  string rc = "";
  MQIMPO impo = {MQIMPO_DEFAULT};
  MQPD pd = {MQPD_DEFAULT};
  char valueBuffer[1024]; // W3C standard recommends a minimum of 512 chars in a tracestate block
  MQLONG valueLength;
  MQCHARV propertyNameVS = {MQCHARV_DEFAULT};
  MQLONG pType;

  propertyNameVS.VSPtr = (PMQVOID)propertyName;
  propertyNameVS.VSLength = MQVS_NULL_TERMINATED;

  impo.Options = MQIMPO_CONVERT_VALUE | MQIMPO_INQ_FIRST;

  pExitParms->Hconfig->MQINQMP_Call(*pHconn, mh, &impo, &propertyNameVS, &pd, &pType, (MQLONG)sizeof(valueBuffer), valueBuffer, &valueLength, pCC, pRC);
  if (*pCC == MQCC_OK) {
    rc = string(valueBuffer, valueLength);
  } else {
    rptmqrc("MQINQ",*pCC, *pRC);
  }
  return rc;
}

// Don't care about the actual value of the property, just that it exists.
bool propsContain(PMQAXP pExitParms, PMQHCONN pHconn, MQHMSG mh, const char *propertyName) {
  bool rc = false;
  MQLONG CC= 0, RC=0;
  propsValue(pExitParms, pHconn, mh, propertyName, &CC, &RC);
  if (CC == MQCC_OK || RC != MQRC_PROPERTY_NOT_AVAILABLE) {
    // Other MQRC values indicate it does exist even if we couldn't extract it
    rc = true;
  }
  return rc;
}

// This function can be useful to create a unique key related to the hConn and hObj values
// where the object name is not sufficiently unique
std::string objectKey(PMQHCONN hc, PMQHOBJ ho) {
  string s;
  if (!ho || *ho == MQHO_UNUSABLE_HOBJ) {
    s = std::to_string(*hc) + "/*";
  } else {
    s = std::to_string(*hc) + "/" + std::to_string(*ho);
  }
  return s;
}

// Do we have a MsgHandle for this hConn? If not, create a new one
MQHMSG getMsgHandle(PMQAXP pExitParms, PMQHCONN pHconn, PMQHOBJ pHobj) {
  string key;
  MQHMSG mh = MQHM_UNUSABLE_HMSG;

  key = objectKey(pHconn, pHobj);
  if (objectHandleMap.count(key) == 0) {
    MQCMHO cmho = {MQCMHO_DEFAULT};
    MQLONG CC, RC;

    pExitParms->Hconfig->MQCRTMH_Call(*pHconn, &cmho, &mh, &CC, &RC);
    if (CC == MQCC_OK) {
      objectHandleMap[key] = mh;
    }
  } else {
    mh = objectHandleMap[key];
  }
  return mh;
}

// Is the GMO/PMO MsgHandle one that we allocated?
bool compareMsgHandle(PMQHCONN pHconn, PMQHOBJ pHobj, MQHMSG mh) {
  bool rc = false;
  string key = objectKey(pHconn, pHobj);
  if (objectHandleMap.count(key) == 1) {
    MQHMSG mhLocal = objectHandleMap[key];
    if (mhLocal == mh) {
      rc = true;
    }
  }
  return rc;
}

extern "C" {
MQ_DISC_EXIT mqotDiscAfter;

// Initialise the module. Set up logging, check and report versions. This
// should be once per process
MQLONG mqotInit(RPT_FN _rpt, char *buf, size_t len) {
  MQLONG rc = MQRC_NONE;

  if (initialised) {
    snprintf(buf,len,"Already initialised");
    rc = MQRC_ALREADY_CONNECTED;  // Not really intended for this, but seems appropriate.
    return rc;
  }

  initialised = true;

  rptMain = _rpt;
  snprintf(buf, len, "Build  : Lib %s ABI %d Bld %s", OPENTELEMETRY_VERSION, OPENTELEMETRY_ABI_VERSION_NO, __DATE__);

  // This structure has the versions from the runtime library
  opentelemetry::plugin::LoaderInfo l;
  auto otel_ver = string(l.opentelemetry_version);
  auto abi_ver = string(l.opentelemetry_abi_version);
  auto abi_ver_int = stoi(abi_ver);
  rpt("Runtime: Lib %s ABI %s", string(otel_ver).c_str(), string(abi_ver).c_str());
  if (abi_ver_int != REQUIRED_ABI) {
    rc = MQRC_WRONG_VERSION; // Another slight misuse of an existing MQRC value
    snprintf(buf, len, "Application built with ABI %d but this exit requires ABI %d", abi_ver_int, REQUIRED_ABI);
  }
  return rc;
}

void mqotTerm() {
  rpt("mqotTerm");
  initialised = false;

  return;
}

void mqotDiscBefore(PMQAXP pExitParms, PMQAXC pExitContext, PPMQHCONN ppHconn, PMQLONG pCompCode, PMQLONG pReason) {
  PMQHCONN pHconn = *ppHconn;
  // Delete anything in the map for this hConn. Need to know the hConn so can't do it in the After.
  // It's OK to delete, even if the DISC were to fail

  // This string is the start of all keys for this hConn
  string key = std::to_string(*pHconn) + "/";

  // The objectHandleMap only holds the HMSG values. No further cleanup needed
  for (auto it = objectHandleMap.begin(); it != objectHandleMap.end();) {
    string mapKey = it->first;
    if (mapKey.find(key, 0) == 0) { // startswith()
      it = objectHandleMap.erase(it);
    }
  }

  // The objectOptionsMap holds structures that were malloced
  for (auto it = objectOptionsMap.begin(); it != objectOptionsMap.end();) {
    string mapKey = it->first;
    if (mapKey.find(key, 0) == 0) { // startswith()
      auto o = objectOptionsMap[key];
      mqotFree(o);
      it = objectOptionsMap.erase(it);
    }
  }
}

// End the "C" block
}
