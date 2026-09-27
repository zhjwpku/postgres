# Copyright (c) 2026, PostgreSQL Global Development Group

arrow_decimal_sources = files('arrow_decimal.c')

if host_system == 'windows'
  arrow_decimal_sources += rc_lib_gen.process(win32ver_rc, extra_args: [
    '--NAME', 'arrow_decimal',
    '--FILEDESC', 'arrow_decimal - fixed-precision decimals with Arrow codecs',])
endif

arrow_decimal = shared_module('arrow_decimal',
  arrow_decimal_sources,
  c_pch: pch_postgres_h,
  kwargs: contrib_mod_args,
)
contrib_targets += arrow_decimal

install_data(
  'arrow_decimal.control',
  'arrow_decimal--1.0.sql',
  kwargs: contrib_data_args,
)

install_headers(
  'arrow_decimal.h',
  install_dir: dir_include_extension / 'arrow_decimal',
)

tests += {
  'name': 'arrow_decimal',
  'sd': meson.current_source_dir(),
  'bd': meson.current_build_dir(),
  'regress': {
    'sql': ['arrow_decimal'],
  },
}
