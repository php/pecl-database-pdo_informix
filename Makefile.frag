#OVERALL_TARGET += $(srcdir)/preprocessed_file.c

#$(srcdir)/ifx.c: $(srcdir)/preprocessed_file.ec $(builddir)/pdo_informix.a
#	(if test -d $(INFORMIXDIR); then \
#	   THREADLIB=POSIX $(INFORMIXDIR)/bin/esql -e $(IFX_ESQL_FLAGS) $(srcdir)/ifx.ec; mv preprocessed_file.c $@; \
#	   THREADLIB=POSIX $(INFORMIXDIR)/bin/esql -e $(IFX_ESQL_FLAGS) $(srcdir)/ifx.ec; \
#           mv preprocessed_file.c $@ || true; \
#	 else \
#	   touch $@; \
#	 fi)

$(builddir)/pdo_informix.a:
	$(LIBTOOL) --mode=link $(CC) $(IFX_LIBOBJS) -o $@

realclean: distclean
	rm -rf acinclude.m4 aclocal.m4 autom4te.cache build config.guess config.h config.h.in config.nice config.sub configure configure.in .deps include install-sh ltmain.sh Makefile.global missing mkinstalldirs modules

.PHONY: realclean
