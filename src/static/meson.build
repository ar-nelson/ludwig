static_sources = [
  'default-theme.css',
  'emoji_table.fb',
  'en.wiki.bpe.vs200000.model',
  'feather-sprite.svg',
  'htmx.min.js',
  'ludwig.js',
  'twemoji-piano.ico'
]

static_gen = []
foreach f : static_sources
  static_gen += custom_target(
    f + '.[h++|S]',
    output: [f + '.h++', f + '.S'],
    input: f,
    command: [binary_header, '@INPUT@', '@OUTDIR@']
  )
endforeach
