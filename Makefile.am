SUBDIRS = src

RELEASE_USER = simsong@
RELEASE_HOST = www.afflib.org
RELEASE_DIR  = afflib.org/

RELEASE_LOC  = $(RELEASE_DIR)/downloads/
CHANGES_LOC  = $(RELEASE_DIR)/ChangeLog.txt
RELEASE_FN   = $(PACKAGE)_version.txt
RELEASE_PATH = $(RELEASE_LOC)/$(RELEASE_FN)
RELEASE_SSH  = $(RELEASE_USER)$(RELEASE_HOST):$(RELEASE_LOC)
RELEASE	     = $(PACKAGE)-$(VERSION)

EXTRA_DIST = aimage.spec aimage.spec.in


prerelease:
	@echo Building and uploading the release...
	@echo Making release $(RELEASE)
	@echo
	echo
	wget http://$(RELEASE_HOST)/downloads/$(RELEASE_FN)
	@echo Version `cat $(RELEASE_FN)` is on the server.
	python checkversion.py $(RELEASE_FN) $(RELEASE)
	/bin/rm $(RELEASE_FN)
	make distcheck
	scp $(RELEASE).tar.gz $(RELEASE_SSH)

release:
	make prerelease
	ssh $(RELEASE_HOST) 'echo $(RELEASE).tar.gz > $(RELEASE_PATH)'
	ssh $(RELEASE_HOST) 'cd $(RELEASE_LOC);/bin/rm $(PACKAGE).tar.gz;ln -s $(RELEASE).tar.gz $(PACKAGE).tar.gz'
	@echo Release $(RELEASE) uploaded to server

renew renew-on-mac:
	touch README.md ChangeLog
	aclocal
	autoheader -f
	autoconf -f
	automake --add-missing -c

