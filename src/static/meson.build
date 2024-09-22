static_sources = [
  'emoji_table.fb',
  'en.wiki.bpe.vs200000.model',
  'feather-sprite.svg',
  'htmx.min.js',
  'ludwig.js',
  'twemoji-piano.ico',
]

minified_css = custom_target(
  'default-theme.min.css',
  output: 'default-theme.min.css',
  input: 'default-theme.css',
  command: [
    find_program('deno'),
    'run',
    '--allow-read',
    '--allow-env',
    '--allow-write=@OUTDIR@',
    'npm:clean-css-cli',
    '@INPUT@',
    '-o', '@OUTPUT@',
  ],
)
static_gen = [
  custom_target(
    'default-theme.min.css.[h++|S]',
    output: ['default-theme.min.css.h++', 'default-theme.min.css.S'],
    input: minified_css,
    command: [binary_header, '@INPUT@', '@OUTDIR@'],
    depends: minified_css,
  ),
]

foreach f : static_sources
  static_gen += custom_target(
    f + '.[h++|S]',
    output: [f + '.h++', f + '.S'],
    input: f,
    command: [binary_header, '@INPUT@', '@OUTDIR@'],
  )
endforeach