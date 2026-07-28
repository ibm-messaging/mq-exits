# CipherSpec Replacer PreConnect Exit

## Introduction

The **CipherSpec Replacer** is an IBM MQ client-side
[PreConnect exit](https://www.ibm.com/docs/en/ibm-mq/latest?topic=functions-api-exits-clients#q109690___title__2) written in C. It intercepts every `MQCONN` or `MQCONNX` call before it reaches the queue manager and transparently rewrites a configurable list of deprecated or banned TLS CipherSpec values to a modern replacement — without requiring any changes to the application itself.

### Why you would want to use this

IBM MQ 10.0 removed support for TLS 1.0, SSLv3 and a number of TLS 1.2 CipherSpecs that were considered cryptographically weak (e.g. `TLS_RSA_WITH_3DES_EDE_CBC_SHA`, `RC4_MD5_US`). Applications that hard-code one of these values in their channel definition will fail to connect to a queue manager running IBM MQ 10.0 or later with an error such as `MQRC_SSL_INITIALIZATION_ERROR`.

Updating every application at once is rarely practical and in some cases impossible. This exit provides a migration bridge: deploy it on the client side and map each banned CipherSpec to a supported alternative (e.g. `ANY_TLS12_OR_HIGHER`) so that existing applications continue to connect while you work through a phased remediation.

Key properties of the exit:
- **Client-side only** — it runs entirely within the client process; it does not require a server-side exit.
- **Zero application changes** — applications operate as normal with no need to recompile; the substitution is invisible to them.
- **Configurable mapping** — the `CIPHER_MAP[]` table in [`cipherSpecReplacer.c`](cipherSpecReplacer.c) lists every old → new pair; add, remove, or change entries to suit your environment and then recompile the exit.
- **Non-invasive fallthrough** — if a CipherSpec is not in the mapping table, or if no CipherSpec is set at all, the connection proceeds completely unmodified.

### Minimum IBM MQ version

The PreConnect exit API was introduced in **IBM MQ 7.1**. This exit will only work with client libraries at this version or higher.

## Building and Installing

### Prerequisites

- A C compiler (GCC / Clang on UNIX; MSVC on Windows)
- IBM MQ client or server installation (for the header files and import library)

### Building on UNIX (Linux / AIX)

A [`Makefile`](Makefile) is provided. Set `MQ_INSTALLATION_PATH` if your MQ is not installed at `/opt/mqm`, then run `make`:

```bash
# Default install path (/opt/mqm)
make

# Custom install path
make MQ_INSTALLATION_PATH=/usr/mqm
```

This produces `cipherSpecReplacer.so` in the current directory.

To build manually without `make`:

```bash
gcc -Wall -Wextra -fPIC -O2 \
    -I${MQ_INSTALLATION_PATH}/inc \
    -shared \
    -L${MQ_INSTALLATION_PATH}/lib64 \
    -Wl,-rpath,${MQ_INSTALLATION_PATH}/lib64 \
    -lmqm \
    -o cipherSpecReplacer.so \
    cipherSpecReplacer.c
```

### Building on Windows

Use the Visual C++ compiler from a Developer Command Prompt:

```bat
cl /LD /W4 /O2 /I"%MQ_INSTALLATION_PATH%\tools\c\include" cipherSpecReplacer.c "%MQ_INSTALLATION_PATH%\tools\lib64\mqic.lib" /Fe:cipherSpecReplacer.dll
```

This produces `cipherSpecReplacer.dll` in the current directory.

### Installing the exit library

Copy the compiled library into the IBM MQ **exits64** directory under the MQ
data directory. The MQ data directory is typically:

| Platform | Default MQ data directory |
|----------|--------------------------|
| Linux / AIX | `/var/mqm/exits64` |
| Windows | `C:\ProgramData\IBM\MQ\exits64` |

> **Note:** The `exits64` directory is for 64-bit exits. If you are running a
> 32-bit client, use `exits` instead. Ensure the file is readable by the user
> account that runs your MQ client applications.

---

## Usage

Once the library is installed, register the exit with the IBM MQ client by
adding a `PreConnect` stanza to the client's `mqclient.ini` file.

Add the following stanza to `mqclient.ini`, replacing `Module` with the location of your so or dll compiled exit:

```ini
PreConnect:
  Module=/var/mqm/exits64/cipherSpecReplacer.so
  Function=CipherSpecPreConnect
  Data=
  Sequence=1
```

| Field | Value | Notes |
|-------|-------|-------|
| `Module` | Full path to the compiled library | Can also be just the filename if the exit is in the default exit directory. |
| `Function` | `CipherSpecPreConnect` | Entry-point name exported by the exit |
| `Data` | *(empty)* | Not used by this exit |
| `Sequence` | `1` | Order in which exits fire when multiple exits are registered |

### Verifying the exit is active

The exit writes a line to `stderr` for every CipherSpec it rewrites:

```
[CipherSpecExit] Rewriting CipherSpec: 'TLS_RSA_WITH_3DES_EDE_CBC_SHA' -> 'ANY_TLS12_OR_HIGHER'
```

Run a test `MQCONNX` from an application that uses one of the mapped CipherSpecs
and confirm this message appears. If the connection succeeds and the message is
present, the exit is working correctly.

---

## Modifications

All configuration is done by editing [`cipherSpecReplacer.c`](cipherSpecReplacer.c)
and recompiling. No external configuration files or environment variables are
read at runtime.

### Adding, modifying or removing entries

By default, every deprecated cipher is mapped to `ANY_TLS12_OR_HIGHER`, which
tells IBM MQ to negotiate the strongest mutually supported TLS 1.2 (or later)
cipher. If your environment requires a specific cipher suite — for example
because a security policy mandates `TLS_RSA_WITH_AES_256_CBC_SHA256` — change
the `new_spec` value in `CIPHER_MAP[]`:

```c
{ "TLS_RSA_WITH_3DES_EDE_CBC_SHA", "TLS_RSA_WITH_AES_256_CBC_SHA256" },
```

Valid CipherSpec strings are listed in the IBM MQ documentation (see
[References](#references) below). The replacement value must be no longer than
32 characters (`MQ_SSL_CIPHER_SPEC_LENGTH`).

You can also add a new `{ "OLD_SPEC", "NEW_SPEC" }` line anywhere inside `CIPHER_MAP[]`
**before** the `{ NULL, NULL }` sentinel to perform other mappings. To stop rewriting a particular CipherSpec, simply delete its line.

Each entry in `CIPHER_MAP[]` can map to a different target. For example:

```c
static const CIPHER_MAPPING CIPHER_MAP[] = {
    { "TLS_RSA_WITH_3DES_EDE_CBC_SHA",  "ANY_TLS13"              },
    { "RC4_MD5_US",                     "ANY_TLS12_OR_HIGHER"    },
    { "NULL_SHA",                       "TLS_RSA_WITH_AES_256_CBC_SHA256" },
    { NULL, NULL }  /* sentinel — do not remove */
};
```

### Replacing the logging mechanism

In its current form the exit writes substitution messages to `stderr`. For
production use you may want to write to a dedicated log file. Replace the
`fprintf(stderr, ...)` call inside `CipherSpecPreConnect` with your preferred
logging approach.

---

## References

- [IBM MQ documentation — PreConnect exit](https://www.ibm.com/docs/en/ibm-mq/latest?topic=functions-api-exits-clients#q109690___title__2)
- [IBM MQ documentation — Referencing connection definitions using a pre-connect exit from a repository](https://www.ibm.com/docs/en/ibm-mq/latest?topic=ueaemis-referencing-connection-definitions-using-pre-connect-exit-from-repository)
- [IBM MQ documentation — SSL/TLS CipherSpec table](https://www.ibm.com/docs/en/ibm-mq/latest?topic=messages-enabling-cipherspecs)
- [IBM MQ documentation — Client configuration file (mqclient.ini)](https://www.ibm.com/docs/en/ibm-mq/latest?topic=multiplatforms-mq-mqi-client-configuration-file-mqclientini)
- [IBM MQ documentation — Changes in IBM MQ 10.0 (removed CipherSpecs)](https://www.ibm.com/docs/en/ibm-mq/10.0.x?topic=1000-whats-changed-in-mq#mq1000_changed__removeciphersupp)
