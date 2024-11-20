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

#include <cstdlib>
#include <cstring>

#include <cmqc.h>
#include <cmqec.h>

#include "mqiotel.hpp"

FILE *fp;

void *mqotMalloc(size_t l) {
  void *p = malloc(l);
  if (!p) {
    fprintf(stderr, "MQIOTEL: Fatal - Cannot allocate %d bytes memory\n", l);
    exit(1);
  }

  return p;
}

void mqotFree(void *p) {
  if (p) {
    free(p);
  }
  return;
}

// This pragma works for GCC (obviously). Might need similar for other compilers
// to get rid of the warnings about functions returning char * to fixed strings.
// Ideally, cmqstrc.h would change to use "const char *" everywhere - but even if done,
// it would only be in future MQ versions. And we need to deal with existing versions.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
#include <cmqstrc.h>
#pragma GCC diagnostic pop
void rptmqrc(const char *verb, MQLONG mqcc, MQLONG mqrc) {
  rpt("MQI Error: %s %d [%s] %d [%s]", verb, mqcc, MQCC_STR(mqcc), mqrc, MQRC_STR(mqrc));
  return;
}

static const char *hexChars = "0123456789ABCDEF";
void dumpHex(const char *title, const void *buf, int length) {
  int i, j;
  unsigned char *p = (unsigned char *)buf;
  int rows;
  int o;
  char line[80];

  if (!fp) {
    return;
  }

  fprintf(fp, "-- %s -- (%d bytes) --------------------\n", title, length);

  rows = (length + 15) / 16;
  for (i = 0; i < rows; i++) {

    memset(line, ' ', sizeof(line));
    o = snprintf(line, sizeof(line) - 1, "%8.8X : ", i * 16);

    for (j = 0; j < 16 && (j + (i * 16) < length); j++) {
      line[o++] = hexChars[p[j] >> 4];
      line[o++] = hexChars[p[j] & 0x0F];
      if (j % 4 == 3)
        line[o++] = ' ';
    }

    o = 48;
    line[o++] = '|';
    for (j = 0; j < 16 && (j + (i * 16) < length); j++) {
      char c = p[j];
      if (!isalnum((int)c) && !ispunct((int)c) && (c != ' '))
        c = '.';
      line[o++] = c;
    }

    o = 65;
    line[o++] = '|';
    line[o++] = 0;

    fprintf(fp, "%s\n", line);
    p += 16;
  }

  return;
}