# About tinydtls

tinydtls is a library for Datagram Transport Layer Security (DTLS)
covering both the client and the server state machine. It is
implemented in C and provides support for a minimal set of cipher
suites suitable for the Internet of Things.

This library contains functions and structures that can help
constructing a single-threaded UDP server with DTLS support in
C99. The following components are available:

* dtls
  Basic support for DTLS with pre-shared key mode and RPK mode with ECC.

* tests
  The subdirectory tests contains test programs that show how each
  component is used.

# BUILDING

When using the code from the git
[repository](https://github.com/eclipse/tinydtls) at GitHub, invoke

    $ ./autogen.sh
    $ ./configure

to re-create the configure script.

## Contiki

On Contiki, place the tinydtls library into the apps folder. After
configuration, invoke make to build the library and associated test
programs. To add tinydtls as Contiki application, drop it into the
apps directory and add the following line to your Makefile:

    APPS += tinydtls/aes tinydtls/sha2 tinydtls/ecc tinydtls

## RIOT

On RIOT, you need to add the line `USEPKG += tinydtls`.
You can use `RIOT/examples/dtls-echo/` as a guide for integrating tinyDTLS
to your application.

Also, if you need a specific commit of tinyDTLS you can modify
`RIOT/pkg/tinydtls/Makefile`.

## CMake

The current cmake support is experimental. Don't hesitate to report issues
and/or provided fixes for it. For general and more details on using CMake,
please consider [CMake - help](https://cmake.org/cmake/help/latest/index.html).

Usage:

```
mkdir tinydtls_build
cd tinydtls_build
cmake -Dmake_tests=ON <path-to-tinydtls>
cmake --build .
```

Available options:

| Option | Description | Default |
| ------ | ----------- | ------- |
| BUILD_SHARED_LIBS | build shared libraries instead of static link library | OFF |
| make_tests | build tests including the examples | OFF |
| DTLS_ECC | enable/disable ECDHE_ECDSA cipher suites | ON |
| DTLS_PSK | enable/disable PSK cipher suites | ON |

# License

Copyright (c) 2011–2022 Olaf Bergmann (TZI) and others.
All rights reserved. This program and the accompanying materials
are made available under the terms of the Eclipse Public License v1.0
and Eclipse Distribution License v. 1.0 which accompanies this distribution.

The Eclipse Public License is available at
http://www.eclipse.org/legal/epl-v10.html and the Eclipse Distribution
License is available at
http://www.eclipse.org/org/documents/edl-v10.php.
