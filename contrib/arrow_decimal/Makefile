# contrib/arrow_decimal/Makefile

MODULE_big = arrow_decimal
OBJS = $(WIN32RES) arrow_decimal.o

EXTENSION = arrow_decimal
DATA = arrow_decimal--1.0.sql
HEADERS_arrow_decimal = arrow_decimal.h
PGFILEDESC = "arrow_decimal - fixed-precision decimals with Arrow codecs"

REGRESS = arrow_decimal

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/arrow_decimal
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
