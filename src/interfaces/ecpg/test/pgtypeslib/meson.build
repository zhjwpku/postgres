# Copyright (c) 2022-2025, PostgreSQL Global Development Group

pgc_files = [
  'dt_test',
  'dt_test2',
  'num_test',
  'num_test2',
  'nan_test',
]

foreach pgc_file : pgc_files
  exe_input = custom_target('@0@.c'.format(pgc_file),
    input: '@0@.pgc'.format(pgc_file),
    command: ecpg_preproc_test_command_start +
      ecpg_preproc_test_command_end,
    kwargs: ecpg_preproc_kw,
  )

  ecpg_test_dependencies += executable(pgc_file,
    exe_input,
    kwargs: ecpg_test_exec_kw,
  )
endforeach
