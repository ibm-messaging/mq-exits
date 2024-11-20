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

#include <map>
#include <string>

#include <stdarg.h>
#include <stdio.h>

typedef void  RPT_FN (const char *fmt, ...);
extern RPT_FN *rptMain;
#define rpt(...) if (rptMain) rptMain( __VA_ARGS__)
extern void rptmqrc(const char *verb, MQLONG mqcc, MQLONG mqrc);

// The W3C names for the properties to be propagated
#define TRACEPARENT "traceparent"
#define TRACESTATE "tracestate"

typedef struct tagHobjOptions hobjOptions;
typedef hobjOptions *phobjOptions;
struct tagHobjOptions {
  MQLONG propCtl; // The PROPCTL attribute on the queue, or -1 if unknown
  PMQGMO gmo;     // Currently-active GMO Options value so we can reset
  PMQPMO pmo;

  // Contents of this is preserved long enough for a PutBefore/After as nothing else
  // can be happening on this hConn in between
  MQPMO  myPmo;
  // Same for GMO
  MQGMO myGmo;
};

using namespace std;
extern map<string, MQHMSG> objectHandleMap;
extern map<string, phobjOptions> objectOptionsMap;

extern string objectKey(PMQHCONN hc, PMQHOBJ ho);

extern bool isValidHandle(MQHMSG mh);
extern MQHMSG getMsgHandle(PMQAXP pExitParms, PMQHCONN pHconn, PMQHOBJ pHobj);
extern bool compareMsgHandle(PMQHCONN pHconn, PMQHOBJ pHobj, MQHMSG mh);

extern bool propsContain(PMQAXP pExitParms, PMQHCONN pHconn, MQHMSG mh, const char *propertyName);
extern string propsValue(PMQAXP pExitParms, PMQHCONN pHconn, MQHMSG mh, const char *propertyName, PMQLONG CC, PMQLONG RC);

extern void *mqotMalloc(size_t l);
extern void mqotFree(void *p);
extern void dumpHex(const char *title, const void *buf, int length);
