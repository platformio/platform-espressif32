---
name: Bug report
about: libcoap crashes, produces incorrect output, or has incorrect behavior
title: ''
labels: ''
assignees: ''

---

----------------------------- Delete Below -----------------------------

INSTRUCTIONS
============

Before submitting a new issue, please follow the checklist and try to find the
answer.

- [ ] I have read the documentation [libcoap Modules Documentation](https://libcoap.net/doc/reference/develop/modules.html)
and the issue is not addressed there.
- [ ] I have read the documentation [libcoap Manual Pages](https://libcoap.net/doc/reference/develop/manpage.html)
and the issue is not addressed there.
- [ ] I have updated my libcoap branch (develop) to the latest version and
checked that the issue is present there.
- [ ] I have searched the [Issue Tracker](https://github.com/obgm/libcoap/issues)
(both open and closed - overwrite `is:issue is:open`) for a similar issue and
not found a similar issue.
- [ ] I have checked the [Wiki](https://github.com/obgm/libcoap/wiki) to see if
the issue is reported there.
- [ ] I have read the HOWTOs provided with the source.
- [ ] I have read the [BUILDING](https://raw.githubusercontent.com/obgm/libcoap/develop/BUILDING)
on how to build from source.

If the issue cannot be solved after checking through the steps above, please
follow these instructions so we can get the needed information to help you in a
quick and effective fashion.

1. Fill in all the fields under **Environment** marked with [ ] by picking the
correct option for you in each case and deleting the others.
2. Describe your problem.
3. Include any debug logs (running the application with verbose logging).
4. Providing as much information as possible under **Other items if possible**
will help us locate and fix the problem.
5. Use [Markdown](https://guides.github.com/features/mastering-markdown/) (see
formatting buttons above) and the Preview tab to check what the issue will look
like.
6. Delete these instructions from the `Delete Below` to the `Delete Above`
marker lines before submitting this issue.

**IMPORTANT: If you do not follow these instructions and provide the necessary
details, it may not be possible to resolve your issue.**

----------------------------- Delete Above -----------------------------

## Environment

- libcoap version (run ``git describe --tags`` to find it):

    // v4.3.0-rc3-41-g25fe796
- Build System:             [Make|CMake]
- Operating System:         [Windows|Linux|macOS|FreeBSD|Cygwin|Solaris|RIOT|Other (which?)]
- Operating System Version: [ ]
- Hosted Environment:       [None|Contiki|LwIP|ESP-IDF|Other (which?)]

## Problem Description

// Detailed problem description goes here.

### Expected Behavior

// Describe what you are expecting.

### Actual Behavior

// Describe what you are seeing.

### Steps to reproduce

1. step1
2. ...


### Code to reproduce this issue

```cpp
// the code should be wrapped in the ```cpp tag so that it will be displayed
better.
#include "coap3/coap.h"

void main()
{

}

```
// If your code is longer than 30 lines, upload it as an attachment.  Do not
include code that is proprietary or sensitive for your project.  Try to reduce
your code as much as possible so that it only demonstrates the issue.

## Debug Logs

```
Debug verbose logs go here.
Please copy the plain text here for us to search the error log. Or attach the
complete logs but leave the main part here if the log is *too* long.
```

## Other items if possible

- [ ] Does what you are trying to do work under any configuration.  Detail what
works.
- [ ] Network configuration that is not straightforward. Detail any networking
that may have NAT or firewalls that might affect what is going on.
