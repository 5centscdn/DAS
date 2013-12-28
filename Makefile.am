# $Id$

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = lib das etc

EXTRA_DIST = autogen.sh das.spec

if BUILDING_RPM
RPMDIR=`$(RPM) --eval %_rpmdir`
SRCRPMDIR=`$(RPM) --eval %_srcrpmdir`
RPMARCH=`$(RPM) --eval %_target_cpu`
BINRPM = $(PACKAGE)-$(VERSION)-$(RPM_RELEASE).$(RPMARCH).rpm
DEBUGRPM = $(PACKAGE)-debuginfo-$(VERSION)-$(RPM_RELEASE).$(RPMARCH).rpm
SRCRPM = $(PACKAGE)-$(VERSION)-$(RPM_RELEASE).src.rpm
rpm: dist
	echo "Building RPM package..."; \
	if $(RPMBUILD) -ta --clean $(PACKAGE)-$(VERSION).tar.gz; then \
		if test -e $(RPMDIR)/$(BINRPM); then \
			$(CP) $(RPMDIR)/$(BINRPM) $(builddir); \
		elif test -e $(RPMDIR)/$(RPMARCH)/$(BINRPM); then \
			$(CP) $(RPMDIR)/$(RPMARCH)/$(BINRPM) $(builddir); \
		fi; \
		test -e $(SRCRPMDIR)/$(SRCRPM) && \
		$(CP) $(SRCRPMDIR)/$(SRCRPM) $(builddir); \
		if test -e $(RPMDIR)/$(DEBUGRPM); then \
			$(CP) $(RPMDIR)/$(DEBUGRPM) $(builddir); \
		elif test -e $(RPMDIR)/$(RPMARCH)/$(DEBUGRPM); then \
			$(CP) $(RPMDIR)/$(RPMARCH)/$(DEBUGRPM) $(builddir); \
		else \
			echo "No debug package; install redhat-rpm-config if you want one."; \
		fi; \
	fi
else
rpm:
	@echo \
	"To build an RPM package, you must run configure with --enable-rpm."; \
	exit 1
endif

if BUILDING_DEB
DEBSOURCEPKG=$(PACKAGE)_$(VERSION).orig.tar.gz
DEBSOURCEDIR=$(PACKAGE)-$(VERSION)
deb: dist
	@echo "Building Debian package..."; \
	mv $(PACKAGE)-$(VERSION).tar.gz $(DEBSOURCEPKG) && \
	tar --extract --gunzip --file $(DEBSOURCEPKG) && \
	mkdir -p $(DEBSOURCEDIR)/debian && \
        cp \
            debian/changelog \
            debian/compat \
            debian/postinst \
            debian/control \
            debian/rules \
            $(DEBSOURCEDIR)/debian && \
	cd $(DEBSOURCEDIR) && \
	$(DEBUILD) -b -rfakeroot -D -us -uc; \
	cd $(srcdir); \
	rm --force $(DEBSOURCEPKG); \
	rm --recursive --force $(DEBSOURCEDIR)
else
deb:
	@echo \
	"To build a Debian package, you must run configure with --enable-deb."; \
	exit 1
endif
