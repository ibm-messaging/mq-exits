#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#  http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
#
#  Copyright (c) IBM Corporation 2026
#
# Makefile — IBM MQ CipherSpec PreConnect exit
#
# Set MQ_INSTALLATION_PATH to your MQ install root before running make.
# Default assumes a standard Linux installation.
#
#   make
#   make MQ_INSTALLATION_PATH=/opt/mqm
#
# Windows users: see the build comment at the top of cipherSpecReplacer.c

MQ_INSTALLATION_PATH ?= /opt/mqm

CC      = gcc
CFLAGS  = -Wall -Wextra -Wno-unused-parameter -fPIC -O2 \
          -I$(MQ_INSTALLATION_PATH)/inc
LDFLAGS = -shared

SRC     = cipherSpecReplacer.c
TARGET  = cipherSpecReplacer.so

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)
