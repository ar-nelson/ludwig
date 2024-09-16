test_sources = [
  'asio_http_client_test.c++',
  'db_test.c++',
  'instance_test.c++',
  'iter_test.c++',
  'jwt_test.c++',
  'remote_media_test.c++',
  'rich_text_test.c++',
  'thumbnailer_test.c++',

  'integration/first_run_setup_test.c++',
  'integration/post_listings_test.c++',
  'integration/registration_test.c++',
]

if not get_option('buildtype').startswith('release')
  test_exe = executable(
    'ludwig_test',
    ludwig_sources,
    vendor_sources,
    fbs_gen,
    static_gen,
    test_sources,
    dependencies: [libs, catch2_dep],
    include_directories: inc_dir,
  )

  test('http_client', test_exe, args: ['[http_client]'])
  test('db', test_exe, args: '[db]', timeout: 60)
  test('instance', test_exe, args: '[instance]')
  test('iter', test_exe, args: '[iter]')
  test('jwt', test_exe, args: '[jwt]')
  test('remote_media', test_exe, args: '[remote_media]')
  test('rich_text', test_exe, args: '[rich_text]')
  test('thumbnailer', test_exe, args: '[thumbnailer]')
  test('first_run', test_exe, args: '[first_run]', timeout: 120)
  test('post_listings', test_exe, args: '[post_listings]', timeout: 120)
  test('registration', test_exe, args: '[registration]', timeout: 120)
endif