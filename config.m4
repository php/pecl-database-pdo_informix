if test "$PHP_PDO" != "no"; then

PHP_ARG_WITH(pdo-informix, for Informix driver for PDO,
[  --with-pdo-informix[=DIR] Include PDO Informix support, DIR is the base
                            Informix install directory, defaults to ${INFORMIXDIR:-nothing}.])

if test "$PHP_PDO_INFORMIX" != "no"; then

  if test -n "$PHP_PDO_INFORMIX" -a "$PHP_PDO_INFORMIX" != "yes"; then
    INFORMIXDIR="$PHP_PDO_INFORMIX"
  else
    if test "$INFORMIXDIR" = ""; then
      AC_MSG_ERROR([INFORMIXDIR environment variable is not set. Please use --with-pdo-informix=<DIR> or set the INFORMIXDIR environment variable.])
    fi
  fi

  AC_MSG_CHECKING([for PDO includes])
  if test -f $abs_srcdir/include/php/ext/pdo/php_pdo_driver.h; then
    pdo_inc_path=$abs_srcdir/ext
  elif test -f $abs_srcdir/ext/pdo/php_pdo_driver.h; then
    pdo_inc_path=$abs_srcdir/ext
  elif test -f $prefix/include/php/ext/pdo/php_pdo_driver.h; then
    pdo_inc_path=$prefix/include/php/ext
  else
    AC_MSG_ERROR([Cannot find php_pdo_driver.h.])
  fi
  AC_MSG_RESULT($pdo_inc_path)

  dnl Don't forget to add additional source files here
  php_pdo_informix_sources_core="pdo_informix.c informix_driver.c informix_statement.c"

  AC_MSG_CHECKING([for includes and libraries])
 
  if test -d "$INFORMIXDIR"; then
    if test ! -d "$INFORMIXDIR/incl/cli"; then
       AC_MSG_ERROR([Cannot find Informix Client SDK includes in $INFORMIXDIR/inc/cli])
    fi  
    if test ! -d "$INFORMIXDIR/incl/esql"; then
       AC_MSG_ERROR([Cannot find ESQL/C includes in $INFORMIXDIR/inc/esql])
    fi  
    if test ! -d "$INFORMIXDIR/$PHP_LIBDIR"; then
       AC_MSG_ERROR([Cannot find Informix libraries in $INFORMIXDIR/$PHP_LIBDIR])
    fi  
    if test ! -d "$INFORMIXDIR/$PHP_LIBDIR/cli"; then
       AC_MSG_ERROR([Cannot find Informix Client SDK libraries in $INFORMIXDIR/$PHP_LIBDIR/cli])
    fi  
    if test ! -d "$INFORMIXDIR/$PHP_LIBDIR/esql"; then
       AC_MSG_ERROR([Cannot find ESQL/C libraries in $INFORMIXDIR/$PHP_LIBDIR/esql])
    fi  
  else
    AC_MSG_ERROR([Informix base installation directory '$INFORMIXDIR' doesn't exist.])
  fi
 
  AC_MSG_RESULT($INFORMIXDIR)

  PHP_ADD_INCLUDE($INFORMIXDIR/incl/cli)
  PHP_ADD_INCLUDE($INFORMIXDIR/incl/esql)
  dnl PHP_ADD_INCLUDE($INFORMIXDIR/incl/<whatever include directory you need>)
  PHP_ADD_LIBPATH($INFORMIXDIR/$PHP_LIBDIR, PDO_INFORMIX_SHARED_LIBADD)
  PHP_ADD_LIBPATH($INFORMIXDIR/$PHP_LIBDIR/cli, PDO_INFORMIX_SHARED_LIBADD)
  PHP_ADD_LIBPATH($INFORMIXDIR/$PHP_LIBDIR/esql, PDO_INFORMIX_SHARED_LIBADD)

  dnl Check if thread safety flags are needed
  if test "$enable_experimental_zts" = "yes"; then
    IFX_ESQL_FLAGS="-thread"   
    CPPFLAGS="$CPPFLAGS -DIFX_THREAD"
  else
    IFX_ESQL_FLAGS=""
  fi

  IFX_LIBS=`THREADLIB=POSIX $INFORMIXDIR/bin/esql $IFX_ESQL_FLAGS -libs`
  IFX_LIBS=`echo $IFX_LIBS | sed -e 's/Libraries to be used://g' -e 's/esql: error -55923: No source or object file\.//g'`

  dnl Seems to get rid of newlines.
  dnl According to Perls DBD-Informix, might contain these strings.

  case "$host_alias" in
    *aix*)
      CPPFLAGS="$CPPFLAGS -D__H_LOCALEDEF";;
  esac

  AC_MSG_CHECKING([Informix version])
  IFX_VERSION=[`$INFORMIXDIR/bin/esql -V | grep "ESQL Version" | sed -ne '1 s/\(.*\)ESQL Version \([0-9]*\)\.\([0-9]*\).*/\2\3/p'`]
  AC_MSG_RESULT($IFX_VERSION)
  AC_DEFINE_UNQUOTED(IFX_VERSION, $IFX_VERSION, [ ])

  if test $IFX_VERSION -ge 900; then
    AC_DEFINE(HAVE_IFX_IUS,1,[ ])
dnl    IFX_ESQL_FLAGS="$IFX_ESQL_FLAGS -EDHAVE_IFX_IUS"
dnl  else
dnl    IFX_ESQL_FLAGS="$IFX_ESQL_FLAGS -EUHAVE_IFX_IUS"
  fi

  PHP_NEW_EXTENSION(pdo_informix, $php_pdo_informix_sources_core, $ext_shared,,-I$pdo_inc_path)

  PHP_ADD_MAKEFILE_FRAGMENT

  PHP_ADD_LIBRARY_DEFER(ifcli, 1, PDO_INFORMIX_SHARED_LIBADD)
  PHP_ADD_LIBRARY_DEFER(ifdmr, 1, PDO_INFORMIX_SHARED_LIBADD)
  

  for i in $IFX_LIBS; do
    case "$i" in
      *.o)
        IFX_LIBOBJS="$IFX_LIBOBJS $i"
        PHP_ADD_LIBPATH($ext_builddir, PDO_INFORMIX_SHARED_LIBADD)
dnl        PHP_ADD_LIBRARY_DEFER(pdo_informix, 1, PDO_INFORMIX_SHARED_LIBADD)
        ;;
      -lm)
        ;;
      -lc)
        ;;
      -l*)
        lib=`echo $i | cut -c 3-`
        PHP_ADD_LIBRARY_DEFER($lib, 1, PDO_INFORMIX_SHARED_LIBADD)
        ;;
      *.a)
        case "`uname -s 2>/dev/null`" in
          UnixWare | SCO_SV | UNIX_SV)
            DLIBS="$DLIBS $i"
            ;;
          *)
            ac_dir="`echo $i|sed 's#[^/]*$##;s#\/$##'`"
            ac_lib="`echo $i|sed 's#^/.*/$PHP_LIBDIR##g;s#\.a##g'`"
            DLIBS="$DLIBS -L$ac_dir -l$ac_lib"
            ;;
        esac
        ;;
    esac
  done

  PHP_SUBST(PDO_INFORMIX_SHARED_LIBADD)
  PHP_SUBST(INFORMIXDIR)
  PHP_SUBST(IFX_LIBOBJS)
  PHP_SUBST(IFX_ESQL_FLAGS)

fi

fi
