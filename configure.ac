AC_INIT([libzseek], [2.1.0], [Fotis Xenakis <foxen@windowslive.com>])

# Before building a new release, the library version info should be modified.
# Apply these instructions sequentially:
#   1. If library source code has changed, increment revision.
#   2. If interfaces have been added/removed/changed, increment current and set revision to 0.
#   3. If interfaces have been added, increment age.
#   4. If interfaces have been removed/changed, set age to 0.
AC_SUBST(LIBZSEEK_CURRENT, 3)
AC_SUBST(LIBZSEEK_REVISION, 1)
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
PKG_CHECK_MODULES([LZ4], [liblz4 >= 1.8.3])
PKG_CHECK_MODULES([CHECK], [check])

AX_IS_RELEASE([git-directory])
AX_COMPILER_FLAGS([WARN_CFLAGS],[WARN_LDFLAGS],,,[ dnl
    -Wunused-macros dnl
    -Wcast-qual dnl
    -Wnull-dereference  dnl
    -Werror=format-security dnl
    -Wno-format-truncation  dnl
    -Wlogical-op  dnl
    -Wduplicated-cond dnl
    -Wduplicated-branches  dnl
    -Wrestrict  dnl
    -Wno-declaration-after-statement dnl
    -Wno-packed  dnl
    -Wno-switch-default dnl
    -Wno-jump-misses-init dnl
])

AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_FILES([libzseek.pc Makefile])
AC_OUTPUT
