# doc/Makefile.am
#
# Copyright (C) 2015-2018 Carsten Schoenert <c.schoenert@t-online.de>
#               2018-2022 Jon Shallow <supjps-libcoap@jpshallow.com>
#
# SPDX-License-Identifier: BSD-2-Clause
#
# This file is part of the CoAP C library libcoap. Please see README and
# COPYING for terms of use.


# We can only perfom the targets in this directory if doxygen is present.

CLEANFILES = \
  doxygen_sqlite3.db

EXTRA_DIST = \
  docbook.local.css \
  upgrade_4.2.1_4.3.0.txt \
  upgrade_4.3.0_4.3.1.txt \
  main.md \
  module_api_wrap.h

if HAVE_DOXYGEN

man-page-cleanup:
	@rm -f $(top_builddir)/doc/DoxygenLayout.xml
	@rm -rf $(top_builddir)/doc/man_tmp
	@rm -rf $(top_builddir)/doc/man_html

man-page-prepare: man-page-cleanup
	@$(MKDIR_P) $(top_builddir)/doc/man_tmp
	@$(MKDIR_P) $(top_builddir)/doc/man_html

man-page-start: man-page-prepare
	## Setup the man page tab
	@echo '' > $(top_builddir)/doc/scratch_insert_file
	@echo '    <tab type="usergroup" visible="yes" url="@ref manpage" title="Manual Pages">' > $(top_builddir)/doc/insert_file
	@echo '     <tab  type="usergroup" visible="yes" url="@ref man_summary" title="Man Pages Summary">' >> $(top_builddir)/doc/insert_file
	##
	@echo '/** @page manpage Manual Pages' > $(top_builddir)/doc/man_tmp/manpage.dox
	@echo '  Here are the libcoap API and Example manual pages:' >> $(top_builddir)/doc/man_tmp/manpage.dox
	@echo '  <table class="directory">' >> $(top_builddir)/doc/man_tmp/manpage.dox
	@echo "   <tr>" >> $(top_builddir)/doc/man_tmp/manpage.dox
	@echo "   <td class=\"entry\" align=\"left\"> @ref man_summary </td><td class=\"desc\" align=\"left\">Man Pages Summary</td>" >> $(top_builddir)/doc/man_tmp/manpage.dox
	@echo "   </tr>" >> $(top_builddir)/doc/man_tmp/manpage.dox
	@echo "   <tr class=\"even\">" >> $(top_builddir)/doc/man_tmp/manpage.dox
	@echo "   <td class=\"entry\" align=\"left\"> @ref man_individual </td><td class=\"desc\" align=\"left\">Individual Man Pages</td>" >> $(top_builddir)/doc/man_tmp/manpage.dox
	@echo "   </tr>" >> $(top_builddir)/doc/man_tmp/manpage.dox
	@echo "  </table>" >> $(top_builddir)/doc/man_tmp/manpage.dox
	@echo ' */' >> $(top_builddir)/doc/man_tmp/manpage.dox
	##
	@echo '/** @page man_summary Manual Pages Summary' > $(top_builddir)/doc/man_tmp/man_summary.dox
	@echo '  Here are a list of libcoap API summary man pages, some of which have code examples, and Examples:' >> $(top_builddir)/doc/man_tmp/man_summary.dox
	@echo '   <table class="directory">' >> $(top_builddir)/doc/man_tmp/man_summary.dox
	##
	@echo '/** @page man_individual Individual Manual Pages' > $(top_builddir)/doc/man_tmp/man_individual.dox
	@echo '  Here are a list of libcoap API individual man pages, some of which have code examples:' >> $(top_builddir)/doc/man_tmp/man_individual.dox
	@echo '   <table class="directory">' >> $(top_builddir)/doc/man_tmp/man_individual.dox
	##
	## Setup the upgrading tab
	@echo '/** @page upgrading Upgrading' > $(top_builddir)/doc/man_tmp/upgrading.dox
	@echo '  Upgrading between libcoap versions:' >> $(top_builddir)/doc/man_tmp/upgrading.dox
	@echo '   <table class="directory">' >> $(top_builddir)/doc/man_tmp/upgrading.dox

man-page-build: upg-page-build man-page-start
	@MAN_FILES=`find $(top_srcdir)/man/ -type f -name "coap.txt.in" ; find $(top_srcdir)/man/ -type f -name "coap_*.in" | ALL=C sort ; find $(top_srcdir)/man/ -type f -name "coap-*.in" | LC_ALL=C sort` ;\
	UPG_FILES=`find $(top_srcdir)/doc/ -type f -name "upgrade_*.txt" | ALL=C sort` ;\
	HTML_FILES=`find $(top_builddir)/man/ -type f -name "*.html"` ;\
	COUNT_MAN_FILES=`echo $${MAN_FILES} | wc -w` ;\
	COUNT_HTML_FILES=`echo $${HTML_FILES} | wc -w` ;\
	## We need the HTML files from the Asciidoc source files, check if they around, otherwise build them.
	if [ "$${COUNT_MAN_FILES}" != "$${COUNT_HTML_FILES}" ]; then \
		$(MAKE) -C ../man ;\
	fi ;\
	##
	## Build the summary man pages
	##
	for FILE in $${MAN_FILES} ; do \
		BASE=`basename $${FILE} | cut -d. -f 1` ;\
		MANUAL=`egrep -B 1 "^====" $${FILE} | head -1` ;\
		SUMMARY=`egrep -B 2 "^SYNOPSIS" $${FILE} | ${SED} 's/coap-//g' | cut -d\- -f2 | cut -c2- | head -1` ;\
		##
		## Fix and copy .html file across
		##
		if [ -f $(top_builddir)/man/$${BASE}.html ]; then \
			## Correct case sensitive Name and Synopsis on master (used later)
			$(SED) -i 's^<h2>Name</h2>^<h2>NAME</h2>^g' $(top_builddir)/man/$${BASE}.html ;\
			$(SED) -i 's^<h2>Synopsis</h2>^<h2>SYNOPSIS</h2>^g' $(top_builddir)/man/$${BASE}.html ;\
			cp -f $(top_builddir)/man/$${BASE}.html $(top_builddir)/doc/man_html/$${BASE}.html ;\
		else \
			echo "ERROR: $(top_builddir)/man/$${BASE}.html not found!";\
			exit 1 ;\
		fi ;\
		## Build the manual insert page
		echo "/// @page man_$${BASE} $${MANUAL}" > $(top_builddir)/doc/man_tmp/$${MANUAL}.dox ;\
		echo "/// @htmlinclude $${BASE}.html $${MANUAL}" >> $(top_builddir)/doc/man_tmp/$${MANUAL}.dox ;\
		## Update insert_file (the list is sorted appropriately)
		echo "      <tab type=\"user\" visible=\"yes\" url=\"@ref man_$${BASE}\" title=\"$${MANUAL} - $${SUMMARY}\" intro=\"\"/>" >> $(top_builddir)/doc/insert_file ;\
		## Update the summary man page
		echo "   <tr$${ROW_EVEN}>" >> $(top_builddir)/doc/man_tmp/man_summary.dox ;\
		echo "   <td class=\"entry\" align=\"left\"> @ref man_$${BASE} </td><td class=\"desc\" align=\"left\">$${SUMMARY}</td>" >> $(top_builddir)/doc/man_tmp/man_summary.dox ;\
		echo "   </tr>" >> $(top_builddir)/doc/man_tmp/man_summary.dox ;\
		if [ -z "$${ROW_EVEN}" ] ; then \
			ROW_EVEN=" class=\"even\"" ;\
		else \
			ROW_EVEN= ;\
		fi \
	done ;\
	##
	## Close off the man page summary file
	##
	echo '   </table>' >> $(top_builddir)/doc/man_tmp/man_summary.dox ;\
	echo ' */' >> $(top_builddir)/doc/man_tmp/man_summary.dox ;\
	echo '     </tab>' >> $(top_builddir)/doc/insert_file ;\
	##
	## Build the individual man pages
	##
	echo '     <tab  type="usergroup" visible="yes" url="@ref man_individual" title="Individual Man Pages">' >> $(top_builddir)/doc/insert_file ;\
	for FILE in $${MAN_FILES} ; do \
		BASE=`basename $${FILE} | cut -d. -f 1` ;\
		LIST=`${SED} -ne '/^NAME/,/^SYNOPSIS/p;/^SYNOPSIS/q' $${FILE} | ${SED} -ne '/coap_/{ s/ *, *//g ; p }' | egrep -v "^$${BASE}$$"` ;\
		for ENTRY in $${LIST} ; do \
			MANUAL="$${ENTRY}(3)" ;\
			## Build the manual insert page
			echo "/// @page man_$${ENTRY} $${MANUAL}" > $(top_builddir)/doc/man_tmp/$${MANUAL}.dox ;\
			echo "/// @htmlinclude $${ENTRY}.html $${MANUAL}" >> $(top_builddir)/doc/man_tmp/$${MANUAL}.dox ;\
			## Create html file
			cat $(top_builddir)/man/$${BASE}.html | ${SED} "s/Function: $${ENTRY}(/<a class=\"anchor\" id=\"$${ENTRY}\"><\/a>\0/" > $(top_builddir)/doc/man_html/$${ENTRY}.html ;\
			## Update scratch_insert_file for sorting later
			echo "$${ENTRY}" >> $(top_builddir)/doc/scratch_insert_file ;\
		done ;\
	done ;\
	##
	## Process the (sorted) list of individual man pages
	##
	for ENTRY in `cat $(top_builddir)/doc/scratch_insert_file | ALL=C sort -u` ; do \
		## Update the individual man page
		MANUAL="$${ENTRY}(3)" ;\
		echo "      <tab type=\"user\" visible=\"yes\" url=\"@ref man_$${ENTRY}\" title=\"$${MANUAL}\" intro=\"\"/>" >> $(top_builddir)/doc/insert_file ;\
		echo "   <tr$${ROW_EVEN}>" >> $(top_builddir)/doc/man_tmp/man_individual.dox ;\
		echo "   <td class=\"entry\" align=\"left\"> @ref man_$${ENTRY} </td><td class=\"desc\" align=\"left\"></td>" >> $(top_builddir)/doc/man_tmp/man_individual.dox ;\
		echo "   </tr>" >> $(top_builddir)/doc/man_tmp/man_individual.dox ;\
		if [ -z "$${ROW_EVEN}" ] ; then \
			ROW_EVEN=" class=\"even\"" ;\
		else \
			ROW_EVEN= ;\
		fi \
	done ;\
	##
	## Close off the individual man pages
	##
	echo '   </table>' >> $(top_builddir)/doc/man_tmp/man_individual.dox ;\
	echo ' */' >> $(top_builddir)/doc/man_tmp/man_individual.dox ;\
	echo '     </tab>' >> $(top_builddir)/doc/insert_file ;\
	##
	## Close off the man page top level
	##
	echo '    </tab>' >> $(top_builddir)/doc/insert_file ;\
	##
	## Add in the deprecated tab
	##
	echo '    <tab type="user" visible="yes" url="@ref deprecated" title="Deprecated Items" intro=""/>' >> $(top_builddir)/doc/insert_file ;\
	##
	## Start the upgrade tab
	##
	echo '    <tab type="usergroup" visible="yes" url="@ref upgrading" title="Upgrading">' >> $(top_builddir)/doc/insert_file ;\
	for FILE in $${UPG_FILES} ; do \
		BASE=`basename $${FILE} | $(SED) "s/\.txt$$//g"`; \
		UPGRADE=`echo $${BASE} | $(SED) "s/^upgrade_//g"`; \
		CUPGRADE=`echo $${UPGRADE} | $(SED) "s/\./-/g"`; \
		SUMMARY=`head -1 $${FILE} | ${SED} 's/^= //g'` ;\
		## Build the upgrade insert page
		echo "/// @page upg_$${CUPGRADE} $${UPGRADE}" > $(top_builddir)/doc/man_tmp/$${UPGRADE}.dox ;\
		echo "/// @htmlinclude $${BASE}.html $${UPGRADE}" >> $(top_builddir)/doc/man_tmp/$${UPGRADE}.dox ;\
		## Update insert_file
		echo "      <tab type=\"user\" visible=\"yes\" url=\"@ref upg_$${CUPGRADE}\" title=\"$${SUMMARY}\" intro=\"\"/>" >> $(top_builddir)/doc/insert_file ;\
		## Update the upgrading page
		echo "   <tr$${ROW_EVEN}>" >> $(top_builddir)/doc/man_tmp/upgrading.dox ;\
		echo "   <td class=\"entry\" align=\"left\"> @ref upg_$${CUPGRADE} </td><td class=\"desc\" align=\"left\">$${SUMMARY}</td>" >> $(top_builddir)/doc/man_tmp/upgrading.dox ;\
		echo "   </tr>" >> $(top_builddir)/doc/man_tmp/upgrading.dox ;\
		if [ -z "$${ROW_EVEN}" ] ; then \
			ROW_EVEN=" class=\"even\"" ;\
		else \
			ROW_EVEN= ;\
		fi ;\
		if [ -f $(top_builddir)/doc/$${BASE}.html ]; then \
			cp -f $(top_builddir)/doc/$${BASE}.html $(top_builddir)/doc/man_html/$${BASE}.html ;\
			## Correct case sensitive Name and Synopsis
			$(SED) -i 's^<div class="toc"><p><strong>^<div class="section"><p><strong>^' $(top_builddir)/doc/man_html/$${BASE}.html ;\
		else \
			echo "ERROR: $(top_builddir)/doc/$${BASE}.html not found!";\
			exit 1 ;\
		fi \
	done ;\
	##
	## Close off the upgrading tab
	##
	echo '   </table>' >> $(top_builddir)/doc/man_tmp/upgrading.dox ;\
	echo ' */' >> $(top_builddir)/doc/man_tmp/upgrading.dox ;\
	##
	## Close off the insert file list
	##
	echo '    </tab>' >> $(top_builddir)/doc/insert_file ;\
	##
	## Create and Update the DoxygenLayout.xml file
	##
	$(DOXYGEN) -l ;\
	$(SED) -i 's/<tab type="pages" visible="yes" /<tab type="pages" visible="no" /g' $(top_builddir)/doc/DoxygenLayout.xml ;\
	$(SED) -i '/<tab type="examples" visible=.*/r insert_file' $(top_builddir)/doc/DoxygenLayout.xml ;\
	##
	## Fix up man html files, adding links
	##
	for FILE in $${MAN_FILES} `cat $(top_builddir)/doc/scratch_insert_file` ; do \
		ENTRY=`basename $${FILE} | cut -d . -f1` ;\
		## Functions defined in the body
		$(SED) -i "s^\(<span class=\"strong\"><strong>\)\(coap[_-][0-9a-z_]*\)\(</strong></span>(\|(\)^\1<a class=\"st-desc\" href=\"man_\2.html#\2\" target=\"_self\">\2</a>\3^g" $(top_builddir)/doc/man_html/$${ENTRY}.html ;\
		## The SYNOPSIS entries
		$(SED) -i "s^\(<p><span class=\"strong\"><strong>[a-z0-9_ \*]*\)\(coap_[0-9a-z_]*\)\([(;]\)^\1<a class=\"st-synopsis\" href=\"man_\2.html#\2\" target=\"_self\">\2</a>\3^g" $(top_builddir)/doc/man_html/$${ENTRY}.html ;\
		## Function in NAME and Examples
		$(SED) -i "s^\([ =,] \|[(!>]\|\^\)\(coap_[0-9a-z_]*\)\([(,]\| \-\| \xe2\x80\x94\)^\1<a href=\"man_\2.html#\2\" target=\"_self\">\2</a>\3^g" $(top_builddir)/doc/man_html/$${ENTRY}.html ;\
		## Do for a second time in case of overlaps
		$(SED) -i "s^\([ =,] \|[(!>]\|\^\)\(coap_[0-9a-z_]*\)\([(,]\| \-\| \xe2\x80\x94\)^\1<a href=\"man_\2.html#\2\" target=\"_self\">\2</a>\3^g" $(top_builddir)/doc/man_html/$${ENTRY}.html ;\
	done ;\
	##
	## Do the highlighting
	##
	for ENTRY in `cat $(top_builddir)/doc/scratch_insert_file` ; do \
		${SED} -i "s/\($${ENTRY}\)\([<(\*, ]\)/<span class=\"man-highlight\">\1<\/span>\2/g" $(top_builddir)/doc/man_html/$${ENTRY}.html ;\
	done

if BUILD_MANPAGES
UPG_LIST = upgrade_4.2.1_4.3.0.txt upgrade_4.3.0_4.3.1.txt

upg-page-build:
	@for FILE in $(UPG_LIST) ; do \
		$(A2X) -d article --format xhtml -D $(top_builddir)/doc/ $(top_srcdir)/doc/$${FILE} ;\
	done

all: man-page-build
	$(DOXYGEN) Doxyfile
	@$(RM) $(top_builddir)/doc/insert_file $(top_builddir)/doc/scratch_insert_file
	@cp -f $(top_srcdir)/doc/docbook.local.css $(top_builddir)/doc/html/docbook-xsl.css

else
#
# Need to make sure the man directories are in place, but empty
all: man-page-prepare
	$(DOXYGEN) Doxyfile
endif # BUILD_MANPAGES

clean-local:
	-rm -rf $(top_builddir)/doc/html $(top_builddir)/doc/man_tmp $(top_builddir)/doc/man_html $(top_builddir)/doc/DoxygenLayout.xml $(top_builddir)/doc/docbook-xsl.css $(top_builddir)/doc/*.html

distclean-local: clean-local

endif # HAVE_DOXYGEN

install-data-hook:
if HAVE_DOXYGEN
	@if [ ! -d $(top_builddir)/doc/html ]; then \
		echo ;\
		echo "     No install data in '$(top_builddir)/doc/html' found! Please run 'make all' first." ;\
		echo ;\
		exit 1 ;\
	fi
	$(MKDIR_P) $(DESTDIR)$(htmldir)/html || exit 1
	cp -a -f $(top_builddir)/doc/html $(DESTDIR)$(htmldir)
	find $(DESTDIR)$(htmldir) -type f -name "*.md5" -delete
endif # HAVE_DOXYGEN
if BUILD_LICENSE_INSTALL
	$(MKDIR_P) $(DESTDIR)$(docdir) || exit 1
	$(INSTALL_DATA) $(top_srcdir)/LICENSE $(DESTDIR)$(docdir)
	$(INSTALL_DATA) $(top_srcdir)/COPYING $(DESTDIR)$(docdir)
	$(INSTALL_DATA) $(top_srcdir)/README $(DESTDIR)$(docdir)
endif # BUILD_LICENSE_INSTALL

uninstall-hook:
if BUILD_LICENSE_INSTALL
	@if [ -d $(DESTDIR)$(docdir) ] ; then \
		(cd $(DESTDIR)$(docdir) ; rm -f LICENSE README COPYING) ; \
	fi
endif # BUILD_LICENSE_INSTALL
if HAVE_DOXYGEN
	-rm -rf $(DESTDIR)$(htmldir)/html
endif # HAVE_DOXYGEN

.PHONY: man-page-cleanup man-page-prepare man-page-start man-page-build
