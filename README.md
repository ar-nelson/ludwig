# 🎹 Ludwig

Ludwig is a [Lemmy][lemmy]-compatible Reddit-style link board and forum server,
available as a zero-configuration single binary.

The current release of Ludwig is an **alpha-quality** MVP. Many features are
missing. Major bugs almost certainly exist. Breaking changes to the database are
possible, and will require exporting and re-importing the entire database to
upgrade.

## MVP Features

- A Reddit-style link board with posts, comments, voting, link previews, and
  user-creatable sub-boards.
- Minimal web interface is usable without JavaScript; users or admins can
  disable JS entirely.
- User signups can be restricted with admin approval or invite codes.
- The Lemmy API is supported: Lemmy apps can interact with the site, read, vote,
  and post.

## Usage

Currently you have to build Ludwig manually; I recommend using the Docker
container (`docker/Dockerfile`) to build and run Ludwig, since there are a lot
of specific dependencies and the current build does not produce a fully static
executable.

Run `task build-release` from within the Docker container, then copy the
`ludwig` binary from `build.release` to somewhere else and run it.

Ludwig will create two `.mdb` files in its directory by default: `ludwig.mdb`
contains the site database, and `search.mdb` is a full-text search index.

By default, Ludwig serves a web interface on port 2023. Just put this behind a
reverse proxy, and it should be ready to go.

### Options

Ludwig does not have a configuration file, but it does accept a few command-line
options.

- `--setup`: Run an interactive first-run setup wizard on the terminal, instead
  of in the web interface. Can only be used when the site is not yet set up.

- `--log-level`: One of `debug`, `info`, `warn`, `error`, or `critical`. Default
  is `info`. Logs are currently written to the terminal; future versions will
  support logging to files.

- `-p N`, `--port=N`: Sets the port for the web interface to `N`.

- `-s N`, `--map-size=N`: The maximum LMDB database size, in MiB. Default is
  4096 (4 GiB). Currently the server will crash if this database size is
  exceeded; future versions will handle low space more gracefully. Also sets the
  maximum size of the search database, if applicable.

  Can be safely increased, but not decreased.

- `--db=FILE`: The database filename; will be created if it does not exist.
  Default is `ludwig.mdb`.

- `--search=PROVIDER`: Full-text search configuration. Default is
  `lmdb:search.mdb`. Potential providers are:

  - `none` - Full-text search is disabled.

  - `lmdb:FILE` - Use [sentencepiece][sentencepiece] to build an LMDB search
    index of tokens. This approach is very limited: it only has token mappings
    for English, and it can only check whether posts contain all of the searched
    tokens, but not the tokens' order. `FILE` is the filename of the search
    database.

  More search providers will be added in the future.

  Note that, currently, switching search providers will only index future posts;
  if you change from `none` to `lmdb` or switch `lmdb` files, the new search
  index will not contain any posts from before the switch.

- `-r N`, `--rate-limit=N`: Maximum HTTP requests allowed per 5 minutes from a
  single IP. POST and PUT requests count as 10 requests. Default is 3000.

- `-t N`, `--threads=N`: Number of request handler threads. Default is the
  number of physical cores on the machine.

- `--export=FILE`: Exports the database to a dump file, then exits.

- `--import=FILE`: Imports a database from a dump file, then exits. The database
  (`--db`) must not exist yet; import will refuse to run if it would overwrite
  an existing database.

## FAQ

### Why clone Lemmy?

A few reasons:

- I like the idea of a Fediverse forum and a Reddit clone, but Lemmy is
  annoyingly difficult to set up.
- I wanted a few specific features, like single-board sites and old-school forum
  homepages.
- Lemmy is AGPL. I prefer permissive licenses; many developers (myself included)
  are reluctant to even read AGPL-encumbered code for fear of legal liability if
  we then write something similar in future projects.

### Why C++, when Lemmy is already written in Rust?

Honestly, it's because I wanted to try a complex project to finally learn C++.
I've already written a lot of C, so I'm familiar enough with manual memory
management and the related pitfalls. C++ was easier in some ways, harder in
others.

I knew I wanted to use LMDB, and that requires a language with manual memory
management. I hadn't used C++ or Rust before, and, since this is a clean-room
reimplementation of Lemmy, I didn't want to use Rust and likely duplicate a lot
of Lemmy's code anyway. [uWebSockets][uws] is also cool, and can be incredibly
fast.

### Why "Ludwig"?

Because, unlike Lemmy, my project is named after the _best_ Koopaling.

## Roadmap

- [ ] MVP release
  - [x] Async write locking (performance improvements)
  - [ ] User settings
  - [ ] Post editing
  - [ ] Post deletion
  - [ ] Board list
  - [ ] User list
  - [ ] Notifications
  - [ ] DMs
  - [ ] Media uploads
  - [ ] Integration tests
  - [ ] Docker build
- [ ] Board moderators
- [ ] Mod log
- [ ] RSS feeds
- [ ] Pinned threads
- [ ] Full static linking with musl
- [ ] Aarch64 build
- [ ] Rich text editor UI
- [ ] Single-board homepage
- [ ] Curated board list homepage
- [ ] Alternate logging backends (file, network?, syslog?)
- [ ] Subscribe to users
- [ ] Discussion languages
- [ ] Track number of active users per board
- [ ] Detect and handle low database space
- [ ] CAPTCHAs and other spam prevention ([email domain blocklists?][spam])
- [ ] Custom themes and JS
- [ ] Custom emoji
- [ ] [Sonic][sonic] search provider
- [ ] Gracefully handle switching search providers
- [ ] Federation
  - [ ] Webfinger and nodeinfo endpoints
  - [ ] ActivityPub compatibility with Lemmy
  - [ ] Parse HTML messages from AP
  - [ ] Backfill new federated boards using the Lemmy API
- [ ] Archive/compress old database entries when low on space
- [ ] Emoji reactions
- [ ] Board-specific user flair
- [ ] More board types (Mastodon-compatible groups, Q&A boards)
- [ ] Gemini support
- [ ] Create a board from an RSS feed

[lemmy]: https://join-lemmy.org/
[uws]: https://github.com/uNetworking/uWebSockets
[spam]: https://github.com/disposable-email-domains/disposable-email-domains/
[sonic]: https://github.com/valeriansaliou/sonic
