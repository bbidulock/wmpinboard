## Process this file with automake to produce Makefile.in

man_MANS = wmpinboard.1

EXTRA_DIST = $(man_MANS) *.pod

wmpinboard.1: wmpinboard.pod
	-pod2man --release=$(VERSION) --center=wmpinboard wmpinboard.pod wmpinboard.1
