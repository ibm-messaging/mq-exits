/*
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  Copyright (c) IBM Corporation 2026
 */
/*********************************************************************
 * cipherSpecReplacer.c
 *
 * IBM MQ PreConnect exit — CipherSpec rewriter
 * 
 * Authors
 * -------
 * Rob Parker, IBM MQ, parrobe@uk.ibm.com
 * IBM Bob, AI code assistant
 *
 * Purpose
 * -------
 * Intercepts every MQCONNX call before it reaches the queue manager
 * and replaces a configurable list of deprecated/banned CipherSpec
 * values with a modern replacement.  This is useful when you cannot
 * immediately change every application that hard-codes an old cipher
 * spec (e.g. TLS_RSA_WITH_3DES_EDE_CBC_SHA) but you need the queue
 * manager to run on IBM MQ 10 which no longer supports them.
 *
 * The mapping table is defined in CIPHER_MAP[] below.  Edit it to
 * suit your environment, then recompile.
 *
 * Building
 * --------
 * Linux / AIX (shared library):
 *   gcc -shared -fPIC -o cipherSpecReplacer.so \
 *       cipherSpecReplacer.c \
 *       -I${MQ_INSTALLATION_PATH}/inc \
 *       -L${MQ_INSTALLATION_PATH}/lib64 -lmqic
 *
 * Windows (DLL):
 *   cl /LD /I"%MQ_INSTALLATION_PATH%\tools\c\include" \
 *      cipherSpecReplacer.c \
 *      "%MQ_INSTALLATION_PATH%\tools\lib64\mqic.lib" \
 *      /Fe:cipherSpecReplacer.dll /EXPORT:CipherSpecPreConnect
 *
 * Registration (mqclient.ini or MQSCO/PreConnect stanza)
 * -------------------------------------------------------
 * Add the following to the client's mqclient.ini file:
 *
 *   PreConnect:
 *     Module=/path/to/cipherSpecReplacer.so
 *     Function=CipherSpecPreConnect
 *     Data=
 *     Sequence=1
 *
 * Notes
 * -----
 * - The exit runs in the CLIENT process, not the queue manager.
 * - It only fires on MQCONNX calls that supply an CipherSpec within the MQCD structure.
 *   Applications that use plain MQCONN are unaffected.
 * - The exit logs every substitution to stderr so you can see it
 *   working in development; replace with a file-based logger for
 *   production use.
 *
 *********************************************************************/

#include <stdio.h>
#include <string.h>

/* MQ client headers */
#include <cmqc.h>        /* Core MQ types: MQHCONN, MQLONG, etc.    */
#include <cmqxc.h>       /* Exit structures: MQNXP, MQCD, MQSCO     */
#include <cmqbc.h>       /* PreConnect exit prototype                */

#if (MQAT_DEFAULT == MQAT_WINDOWS_NT)
  #define MQEXPORT __declspec(dllexport)
#else
  #define MQEXPORT 
#endif
/* ----------------------------------------------------------------- */
/* CipherSpec mapping table                                           */
/* Add as many OLD -> NEW pairs as you need.                          */
/* Both strings must be <= MQ_SSL_CIPHER_SPEC_LENGTH (32) characters. */
/* ----------------------------------------------------------------- */
typedef struct {
    const char *old_spec;
    const char *new_spec;
} CIPHER_MAPPING;

static const CIPHER_MAPPING CIPHER_MAP[] = {
    /* TLS 1.0 */
    { "AES_SHA_US",                      "ANY_TLS12_OR_HIGHER" },
    { "TLS_RSA_EXPORT_WITH_RC2_40_MD5",  "ANY_TLS12_OR_HIGHER" },
    { "TLS_RSA_EXPORT_WITH_RC4_40_MD5",  "ANY_TLS12_OR_HIGHER" },
    { "TLS_RSA_WITH_3DES_EDE_CBC_SHA",   "ANY_TLS12_OR_HIGHER" },
    { "TLS_RSA_WITH_AES_128_CBC_SHA",    "ANY_TLS12_OR_HIGHER" },
    { "TLS_RSA_WITH_AES_256_CBC_SHA",    "ANY_TLS12_OR_HIGHER" },
    { "TLS_RSA_WITH_DES_CBC_SHA",        "ANY_TLS12_OR_HIGHER" },
    { "TLS_RSA_WITH_NULL_MD5",           "ANY_TLS12_OR_HIGHER" },
    { "TLS_RSA_WITH_NULL_SHA",           "ANY_TLS12_OR_HIGHER" },
    { "TLS_RSA_WITH_RC4_128_MD5",        "ANY_TLS12_OR_HIGHER" },
    { "TLS_RSA_WITH_RC4_128_SHA",        "ANY_TLS12_OR_HIGHER" },

    /* SSL 3.0 */
    { "DES_SHA_EXPORT",                  "ANY_TLS12_OR_HIGHER" },
    { "DES_SHA_EXPORT1024",              "ANY_TLS12_OR_HIGHER" },
    { "FIPS_WITH_3DES_EDE_CBC_SHA",      "ANY_TLS12_OR_HIGHER" },
    { "FIPS_WITH_DES_CBC_SHA",           "ANY_TLS12_OR_HIGHER" },
    { "NULL_MD5",                        "ANY_TLS12_OR_HIGHER" },
    { "NULL_SHA",                        "ANY_TLS12_OR_HIGHER" },
    { "RC2_MD5_EXPORT",                  "ANY_TLS12_OR_HIGHER" },
    { "RC4_56_SHA_EXPORT1024",           "ANY_TLS12_OR_HIGHER" },
    { "RC4_MD5_EXPORT",                  "ANY_TLS12_OR_HIGHER" },
    { "RC4_MD5_US",                      "ANY_TLS12_OR_HIGHER" },
    { "RC4_SHA_US",                      "ANY_TLS12_OR_HIGHER" },
    { "TRIPLE_DES_SHA_US",               "ANY_TLS12_OR_HIGHER" },

    /* Sentinel — do not remove */
    { NULL, NULL }
};

/* ----------------------------------------------------------------- */
/* Helper: copy a fixed-width MQ character field into a NUL-          */
/* terminated C string, trimming trailing spaces.                     */
/* ----------------------------------------------------------------- */
static void mqfield_to_str(const char *field, MQLONG field_len,
                           char *buf, size_t buf_len)
{
    MQLONG copy_len = field_len < (MQLONG)(buf_len - 1)
                          ? field_len
                          : (MQLONG)(buf_len - 1);
    memcpy(buf, field, (size_t)copy_len);
    buf[copy_len] = '\0';

    /* Trim trailing spaces (MQ pads with blanks, not NUL) */
    char *end = buf + copy_len - 1;
    while (end >= buf && *end == ' ')
        *end-- = '\0';
}

/* ----------------------------------------------------------------- */
/* Helper: write a NUL-terminated string into an MQ fixed-width field,*/
/* padding with spaces to fill field_len bytes.                       */
/* ----------------------------------------------------------------- */
static void str_to_mqfield(const char *str, char *field, MQLONG field_len)
{
    MQLONG str_len = (MQLONG)strlen(str);
    MQLONG copy_len = str_len < field_len ? str_len : field_len;
    memcpy(field, str, (size_t)copy_len);
    if (copy_len < field_len)
        memset(field + copy_len, ' ', (size_t)(field_len - copy_len));
}

/* ----------------------------------------------------------------- */
/* PreConnect exit entry point                                         */
/*                                                                    */
/* Signature mandated by IBM MQ:                                      */
/*   void MQ_PRECONNECT_EXIT(PMQNXP, PMQCHAR, PPMQCNO, PMQLONG, PMQLONG)        */
/* ----------------------------------------------------------------- */
MQEXPORT void CipherSpecPreConnect(
    PMQNXP  pExitParms,   /*PreConnect exit parameter structure*/
    PMQCHAR pQMgrName,    /*Name of the queue manager*/
    PPMQCNO ppConnectOpts,/*Options controlling the action of MQCONNX*/
    PMQLONG pCompCode,    /*Completion code*/
    PMQLONG pReason       /*Reason qualifying pCompCode*/
    )
{
    char current_spec[MQ_SSL_CIPHER_SPEC_LENGTH + 1];
    PMQCD pChannelDef = NULL;
    PMQCNO pConnectOpts = NULL;

    /* Default: let the connection proceed unchanged */
    *pCompCode = MQCC_OK;
    *pReason   = MQRC_NONE;

    /* Safety checks */
    if (pExitParms == NULL || ppConnectOpts == NULL)
        return;

    /* We only act on the Connect call; ignore Disconnect / Other */
    if (pExitParms->ExitReason != MQXR_PRECONNECT)
        return;
    
    /* Get to the MQCD where the SSLCipherSpec is*/
    pConnectOpts = (PMQCNO)*ppConnectOpts;
    pChannelDef = (PMQCD)pConnectOpts->ClientConnPtr;

    /* Extract the current CipherSpec from the channel definition */
    mqfield_to_str(pChannelDef->SSLCipherSpec,
                   MQ_SSL_CIPHER_SPEC_LENGTH,
                   current_spec,
                   sizeof(current_spec));

    /* Empty CipherSpec — no TLS in use, nothing to rewrite */
    if (current_spec[0] == '\0')
        return;

    /* Search the mapping table */
    for (const CIPHER_MAPPING *m = CIPHER_MAP; m->old_spec != NULL; m++) {
        if (strcmp(current_spec, m->old_spec) == 0) {
            fprintf(stderr,
                    "[CipherSpecExit] Rewriting CipherSpec: '%s' -> '%s'\n",
                    current_spec, m->new_spec);

            str_to_mqfield(m->new_spec,
                           pChannelDef->SSLCipherSpec,
                           MQ_SSL_CIPHER_SPEC_LENGTH);
            return; /* one match is enough */
        }
    }

    /* No match found — leave CipherSpec unchanged */
}
