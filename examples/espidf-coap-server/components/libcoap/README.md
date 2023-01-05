# libcoap: A C implementation of the Constrained Application Protocol (RFC 7252)

[![Build Status: main](https://github.com/obgm/libcoap/actions/workflows/main.yml/badge.svg?branch=main)](https://github.com/obgm/libcoap/actions?query=branch:main)
[![Build Status: develop](https://github.com/obgm/libcoap/actions/workflows/main.yml/badge.svg?branch=develop)](https://github.com/obgm/libcoap/actions?query=branch:develop)
[![Static Analysis](https://scan.coverity.com/projects/10970/badge.svg?flat=1)](https://scan.coverity.com/projects/obgm-libcoap)
[![Fuzzing Status](https://oss-fuzz-build-logs.storage.googleapis.com/badges/libcoap.svg)](https://bugs.chromium.org/p/oss-fuzz/issues/list?sort=-opened&can=1&q=proj:libcoap)

Copyright (C) 2010—2022 by Olaf Bergmann <bergmann@tzi.org> and others

ABOUT LIBCOAP
=============

libcoap is a C implementation of a lightweight application-protocol
for devices that are constrained their resources such as computing
power, RF range, memory, bandwidth, or network packet sizes. This
protocol, CoAP, is standardized by the IETF as RFC 7252. For further
information related to CoAP, see <http://coap.technology>.

You might want to check out
[libcoap-minimal](https://github.com/obgm/libcoap-minimal) for usage
examples.

DOCUMENTATION
=============

Documentation and further information can be found at
<https://libcoap.net>.

PACKAGE CONTENTS
================

This package contains a protocol parser and basic networking
functions for platforms with support for malloc() and BSD-style
sockets. In addition, there is support for Contiki, LwIP and
Espressif/ESP-IDF hosted environments.

The following RFCs are supported

* RFC7252: The Constrained Application Protocol (CoAP)

* RFC7390: Group Communication for the Constrained Application Protocol (CoAP)

* RFC7641: Observing Resources in the Constrained Application Protocol (CoAP)

* RFC7959: Block-Wise Transfers in the Constrained Application Protocol (CoAP)

* RFC7967: Constrained Application Protocol (CoAP) Option for No Server Response

* RFC8132: PATCH and FETCH Methods for the Constrained Application Protocol (CoAP)

* RFC8323: CoAP (Constrained Application Protocol) over TCP, TLS, and WebSockets
  [No WebSockets support]

* RFC8516: "Too Many Requests" Response Code for the Constrained Application Protocol

* RFC8768: Constrained Application Protocol (CoAP) Hop-Limit Option

* RFC9175: CoAP: Echo, Request-Tag, and Token Processing

There is (D)TLS support for the following libraries

* OpenSSL (Minimum version 1.1.0) [PKI, PSK and PKCS11]

* GnuTLS (Minimum version 3.3.0) [PKI, PSK, RPK(3.6.6+) and PKCS11]

* Mbed TLS (Minimum version 2.7.10) [PKI and PSK]

* TinyDTLS [PSK and RPK] [DTLS Only]

The examples directory contain a CoAP client, CoAP Resource Directory server
and a CoAP server to demonstrate the use of this library.

BUILDING
========

Further information can be found at <https://libcoap.net/install.html>
and [BUILDING](https://raw.githubusercontent.com/obgm/libcoap/develop/BUILDING).

LICENSE INFORMATION
===================

This library is published as open-source software without any warranty
of any kind. Use is permitted under the terms of the simplified BSD
license. It includes public domain software. libcoap binaries may also
include open-source software with their respective licensing terms.
Please refer to
[LICENSE](https://raw.githubusercontent.com/obgm/libcoap/develop/LICENSE)
for further details.

