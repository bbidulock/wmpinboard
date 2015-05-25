## Process this file with automake to produce Makefile.in

SUBDIRS = src man
EXTRA_DIST = CREDITS TODO wmpb-convert.pl wmpinboard.lsm \
	     xpm/abar.xpm \
	     xpm/bbar.xpm \
	     xpm/digits.xpm \
	     xpm/pinboard.xpm \
	     themes-kit/abar.xpm \
	     themes-kit/bbar.xpm \
	     themes-kit/board.xpm \
	     themes-kit/default.wmpbtheme \
	     themes-kit/digits.xpm \
	     themes-kit/HOWTO \
	     themes-kit/sample.wmpbtheme

bin-dist: all
	@test -x ../make_bin_package || ( echo "Not in development environment."; false )
	strip src/$(PACKAGE)
	../make_bin_package $(PACKAGE) $(VERSION) i386-linux-glibc wmpb-convert.pl

rpm-dist: dist
	cp $(PACKAGE)-$(VERSION).tar.gz ../SOURCES
	rpm -ba $(PACKAGE).spec

on-site: dist bin-dist rpm-dist
	cp ChangeLog $(HOME)/public_html/stuff/$(PACKAGE)
	cp $(PACKAGE).lsm $(HOME)/public_html/stuff/$(PACKAGE)
	pod2html man/$(PACKAGE).pod >$(HOME)/public_html/stuff/$(PACKAGE)/$(PACKAGE).1.html
	rm -f *~~
	mv ../*RPMS/$(PACKAGE)*rpm ../ON-SITE
	mv $(PACKAGE)-$(VERSION).tar.gz ../ON-SITE
	cd ../ON-SITE && \
	mkdir $(PACKAGE)-tmp && \
	cd $(PACKAGE)-tmp && \
	cp ../$(PACKAGE)*gz . && \
	gz2bz2 $(PACKAGE)*gz && \
	mv $(PACKAGE)*.bz2 .. && \
	cd .. && \
	rmdir $(PACKAGE)-tmp && \
	mv $(PACKAGE)* $(HOME)/public_html/stuff/$(PACKAGE)

