fbs_sources = [
  'db.fbs',
  'emoji_table.fbs'
]

fbs_gen = []
foreach f : fbs_sources
  basename = f.split('.')[0]
  fbs_gen += custom_target(
    basename + '.h++',
    output: basename + '.h++',
    input: f,
    command: [flatc, '-o', '@OUTDIR@', '-I', '@CURRENT_SOURCE_DIR@', '--cpp-std', 'c++17', '--filename-suffix', '', '--filename-ext', 'h++', '-c', '@INPUT@']
  )
  fbs_gen += custom_target(
    f + '.[h++|S]',
    output: [f + '.h++', f + '.S'],
    input: f,
    command: [binary_header, '@INPUT@', '@OUTDIR@']
  )
endforeach
