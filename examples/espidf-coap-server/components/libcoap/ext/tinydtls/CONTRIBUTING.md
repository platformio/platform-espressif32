Contributing to tinydtls
========================

Thanks for your interest in this project.

Project description:
--------------------

tinydtls is a library for Datagram Transport Layer Security (DTLS) covering
both the client and the server state machine. It is implemented in C and
provides support for the mandatory cipher suites specified in CoAP.

- https://projects.eclipse.org/projects/iot.tinydtls

Developer resources:
--------------------

Information regarding source code management, builds, and more.

- https://projects.eclipse.org/projects/iot.tinydtls/developer

Eclipse Contributor Agreement
-----------------------------

Before your contribution can be accepted by the project team contributors must
electronically sign the Eclipse Contributor Agreement (ECA).

* http://www.eclipse.org/legal/ECA.php

Commits that are provided by non-committers must have a Signed-off-by field in
the footer indicating that the author is aware of the terms by which the
contribution has been provided to the project. The non-committer must
additionally have an Eclipse Foundation account and must have a signed Eclipse
Contributor Agreement (ECA) on file.

For more information, please see the Eclipse Committer Handbook:
https://www.eclipse.org/projects/handbook/#resources-commit

Contact:
--------

Contact the project developers via the project's "dev" list.

- https://dev.eclipse.org/mailman/listinfo/tinydtls-dev

Search for bugs:
----------------

This project uses Bugzilla to track ongoing development and issues.

- https://bugs.eclipse.org/bugs/buglist.cgi?product=tinydtls

Create a new bug:
-----------------

Be sure to search for existing bugs before you create another one.
Remember that contributions are always welcome!

- https://bugs.eclipse.org/bugs/enter_bug.cgi?product=tinydtls

Submit Patches via GitHub:
--------------------------

Patches must follow to the tinydtls coding style and must be submitted
as pull request at https://github.com/eclipse/tinydtls for review. To
submit a patch, the author needs to have a Eclipse Contributor Agreement
as explained above.

Every new file must contain the Eclipse license information and the
copyright holder(s). Please take a look into existing files and adopt
the needed changes to your new file(s).

Main and Develop:
-----------------

Please prepare all patches against the "main" branch.

It may take sometimes a little longer for a pull request to be processed
and merged to "main". Therefore some of the pending pull requests will be
available on the "develop" branch as preview. If you want to test a specific
pending pull request which is currently not on "develop", let us know by
adding a comment to that pull request. If a pull request is cherry-picked
to the "develop" branch, that doesn't grant that it is merged as-it-is.
For house-keeping, it may in some cases be required to push the "develop"
branch with "--force-with-lease" in order to adjust the branch for later
changes in a pull request before it gets merged into "main" or if "develop"
is rebased to "main".

In some rare cases, it may be required to include another still pending pull
request/commit into your pull request additionally. If that other pull request
gets merged, please rebase then your pull request using the new "main".

Currently (July 2022) this process change is in progress. Therefore some
pull requests are merged into "develop" and will be included in "main"
after the review finally completes.

Tinydtls Coding style:
----------------------

* For better reading the indentation is set to 2 characters as spaces,
  this is depended on the often used nested functions like
  'if-else'. Don't use TABs any there! Avoid trailing white spaces at
  the end of a line.

* Single lines within the source code should not be longer than 72
  characters and must not be longer than 80.

* In the implementation (i.e., in files ending with '.c'), function
  identifiers start on the first column of a line. The function's
  return type preceeds the function identifier on a line of its
  own. For example, in `dtls.c` the following definition is found:

```
dtls_peer_t *
dtls_get_peer(const dtls_context_t *ctx, const session_t *session) {
...
}
```

* Declarations in header files do not follow the previous rule. For
  example, the declaration for `dtls_get_peer()` in `dtls.h` reads as
  follows:

```
dtls_peer_t *dtls_get_peer(const dtls_context_t *context,
			   const session_t *session);
```

* A useful source code documentation is mandatory. Mostly to be done
  within the source code files.

* Please set up/adjust the doxygen documentation if you create new
  functions or change existing functions. The doxygen documentation
  has to be done in the header files as they are the public part of
  tinydtls and only use the @-syntax for doxygen commands (akin to
  javadoc).

* Never break the API!
  Do not remove old functions unless absolutely necessary. If changes
  are needed in some kind always provide a wrapper for the old call to
  let the library be backward compatible and mark the old function as
  @deprecated in the doxygen comment.  Please discuss needed changes
  on the mailing list.

