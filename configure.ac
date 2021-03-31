AC_INIT([libzseek], [0.1], [Fotis Xenakis <foxen@windowslive.com>])

# Before building a new release, the library version info should be modified.
# Apply these instructions sequentially:
#   1. If library source code has changed, increment revision.
#   2. If interfaces have been added/removed/changed, increment current and set revision to 0.
#   3. If interfaces have been added, increment age.
#   4. If interfaces have been removed/changed, set age to 0.
AC_SUBST(LIBZSEEK_CURRENT, 0)
AC_SUBST(LIBZSEEK_REVISION, 0)
AC_SUBST(LIBZSEEK_AGE, 0)

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([foreign subdir-objects silent-rules])
AM_SILENT_RULES([yes])
AC_CONFIG_SRCDIR([src/zseek.h])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CC_C99
AC_GNU_SOURCE
LT_INIT

AX_PTHREAD([], [AC_MSG_ERROR([requires pthread])])

PKG_CHECK_MODULES([ZSTD], [libzstd >= 1.4.9])

AX_IS_RELEASE([git-directory])
AX_COMPILER_FLAGS([WARN_CFLAGS],[WARN_LDFLAGS])

AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_FILES([libzseek.pc Makefile])
AC_OUTPUT
