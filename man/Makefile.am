## Process this file with automake to produce Makefile.in

dist_man_MANS = wmpinboard.1

EXTRA_DIST = wmpinboard.pod

wmpinboard.1: wmpinboard.pod
	-pod2man --release=$(VERSION) --center=wmpinboard wmpinboard.pod wmpinboard.1

MAINTAINERCLEANFILES = wmpinboard.1
