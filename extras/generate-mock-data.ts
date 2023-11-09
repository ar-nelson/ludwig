// A Deno script to generate a Ludwig database dump with a huge amount of
// realistic mock data.

import {
  Board,
  Comment,
  CommentSortType,
  Dump,
  DumpType,
  Hash,
  LocalBoard,
  LocalUser,
  ModState,
  PlainTextWithEmojis,
  Salt,
  SettingRecord,
  SortType,
  TextBlock,
  TextSpan,
  TextSpans,
  Thread,
  User,
  Vote,
  VoteRecord,
} from "./flatbuffers/ludwig.ts";
import { faker } from "npm:@faker-js/faker";
import {
  Foras,
  GzEncoder,
} from "https://deno.land/x/denoflate@v2.1.4/src/deno/mod.ts";
import { pbkdf2Sync } from "node:crypto";
import * as flatbuffers from "npm:flatbuffers";

await Foras.initBundledOnce();

const PASSWORD_SALT = new TextEncoder().encode("0123456789abcdef");

const SCALE = 100;
let nextId = 0n;
let ludwigDomain = "localhost:2023";

function genInstance(): { id: bigint; domain: string } {
  return { id: nextId++, domain: `${faker.internet.domainWord()}.test` };
}

function genImageUrl(
  width: number,
  height: number = width,
  word = faker.word.noun(),
): string {
  return `https://loremflickr.com/${width}/${height}/${
    faker.helpers.slugify(word)
  }?lock=${faker.number.int(100)}`;
}

function genWikipediaLink(): { url: string; title: string } {
  let title: string;
  switch (faker.number.int(4)) {
    case 0:
      title = faker.science.chemicalElement().name;
      break;
    case 1:
      title = faker.location.country();
      break;
    default:
      title = faker.animal[faker.animal.type() as keyof typeof faker.animal]();
  }
  return {
    url: `https://en.wikipedia.org/wiki/${
      encodeURIComponent(title.trim().replaceAll(" ", "_"))
    }`,
    title,
  };
}

function genUser(
  baseName: string,
  instance?: { id: bigint; domain: string },
): { id: bigint; data: Uint8Array } {
  const fbb = new flatbuffers.Builder(),
    id = nextId++,
    displayName = faker.helpers.maybe(() => faker.person.fullName(), {
      probability: 0.5,
    }),
    bio = faker.helpers.maybe(() => faker.person.bio(), {
      probability: 0.75,
    }),
    avatar = faker.helpers.maybe(
      () =>
        genImageUrl(
          faker.number.int({ min: 32, max: 512 }),
          undefined,
          faker.word.noun(),
        ),
      { probability: 0.75 },
    ),
    banner = faker.helpers.maybe(() => {
      const w = faker.number.int({ min: 320, max: 1920 }),
        h = faker.number.int({ min: 100, max: Math.min(w, 720) });
      return genImageUrl(w, h, faker.word.noun());
    }, { probability: 0.5 });
  fbb.finish(
    User.createUser(
      fbb,
      fbb.createString(
        instance ? `${baseName}@${instance.domain}` : baseName,
      ),
      User.createDisplayNameTypeVector(
        fbb,
        displayName ? [PlainTextWithEmojis.Plain] : [],
      ),
      User.createDisplayNameVector(
        fbb,
        displayName ? [fbb.createString(displayName)] : [],
      ),
      bio ? fbb.createSharedString(bio) : 0,
      User.createBioTypeVector(fbb, bio ? [TextBlock.P] : []),
      User.createBioVector(
        fbb,
        bio
          ? [TextSpans.createTextSpans(
            fbb,
            TextSpans.createSpansTypeVector(fbb, [TextSpan.Plain]),
            TextSpans.createSpansVector(fbb, [fbb.createSharedString(bio)]),
          )]
          : [],
      ),
      instance
        ? fbb.createString(`https://${instance.domain}/ap/actor/${baseName}`)
        : 0,
      instance
        ? fbb.createString(
          `https://${instance.domain}/ap/actor/${baseName}/inbox`,
        )
        : 0,
      instance ? instance.id : null,
      BigInt(faker.date.past().valueOf()) / 1000n,
      null,
      null,
      avatar ? fbb.createString(avatar) : 0,
      banner ? fbb.createString(banner) : 0,
      faker.datatype.boolean(0.05),
      faker.helpers.maybe(() => faker.helpers.enumValue(ModState), {
        probability: 0.05,
      }) ?? ModState.Visible,
      0,
    ),
  );
  return { id, data: fbb.asUint8Array() };
}

function genLocalUser(
  baseName: string,
  isAdmin = false,
  password = faker.internet.password({ length: 8 }),
): Uint8Array {
  const fbb = new flatbuffers.Builder(),
    hash = pbkdf2Sync(
      password,
      PASSWORD_SALT,
      600000,
      32,
      "sha256",
    );
  if (isAdmin) console.warn(`User: ${baseName}, Pass: ${password}`);
  const email = fbb.createString(faker.internet.email());
  LocalUser.startLocalUser(fbb);
  LocalUser.addEmail(fbb, email);
  LocalUser.addPasswordHash(fbb, Hash.createHash(fbb, [...hash]));
  LocalUser.addPasswordSalt(fbb, Salt.createSalt(fbb, [...PASSWORD_SALT]));
  LocalUser.addApproved(fbb, true);
  LocalUser.addShowAvatars(fbb, isAdmin || faker.datatype.boolean(0.8));
  LocalUser.addShowKarma(fbb, isAdmin || faker.datatype.boolean(0.8));
  LocalUser.addHideCwPosts(fbb, !isAdmin && faker.datatype.boolean(0.5));
  LocalUser.addAdmin(fbb, isAdmin);
  fbb.finish(LocalUser.endLocalUser(fbb));
  return fbb.asUint8Array();
}

function genBoard(
  baseName: string,
  instance?: { id: bigint; domain: string },
): { id: bigint; data: Uint8Array } {
  const fbb = new flatbuffers.Builder(),
    id = nextId++,
    displayName = faker.helpers.maybe(
      () => baseName[0].toUpperCase() + baseName.slice(1),
      { probability: 0.75 },
    ),
    description = faker.helpers.maybe(
      () => faker.lorem.paragraphs({ min: 1, max: 3 }),
      { probability: 0.75 },
    ),
    icon = faker.helpers.maybe(
      () =>
        genImageUrl(
          faker.number.int({ min: 32, max: 512 }),
          undefined,
          baseName,
        ),
      { probability: 0.75 },
    ),
    banner = faker.helpers.maybe(() => {
      const w = faker.number.int({ min: 320, max: 1920 }),
        h = faker.number.int({ min: 100, max: Math.min(w, 720) });
      return genImageUrl(w, h, baseName);
    }, { probability: 0.5 });
  fbb.finish(
    Board.createBoard(
      fbb,
      fbb.createString(
        instance ? `${baseName}@${instance.domain}` : baseName,
      ),
      Board.createDisplayNameTypeVector(
        fbb,
        displayName ? [PlainTextWithEmojis.Plain] : [],
      ),
      Board.createDisplayNameVector(
        fbb,
        displayName ? [fbb.createString(displayName)] : [],
      ),
      instance
        ? fbb.createString(`https://${instance.domain}/ap/actor/${baseName}`)
        : 0,
      instance
        ? fbb.createString(
          `https://${instance.domain}/ap/actor/${baseName}/inbox`,
        )
        : 0,
      instance ? instance.id : null,
      BigInt(faker.date.past().valueOf()) / 1000n,
      null,
      description ? fbb.createSharedString(description) : 0,
      Board.createDescriptionTypeVector(
        fbb,
        description ? [TextBlock.P] : [],
      ),
      Board.createDescriptionVector(
        fbb,
        description
          ? [TextSpans.createTextSpans(
            fbb,
            TextSpans.createSpansTypeVector(fbb, [TextSpan.Plain]),
            TextSpans.createSpansVector(fbb, [
              fbb.createSharedString(description),
            ]),
          )]
          : [],
      ),
      icon ? fbb.createString(icon) : 0,
      banner ? fbb.createString(banner) : 0,
      faker.datatype.boolean(0.1)
        ? fbb.createString(faker.company.catchPhrase())
        : 0,
      false,
      false,
      true,
      true,
      SortType.Active,
      CommentSortType.Hot,
      faker.helpers.maybe(() => faker.helpers.enumValue(ModState), {
        probability: 0.05,
      }) ?? ModState.Visible,
      0,
    ),
  );
  return { id, data: fbb.asUint8Array() };
}

function genLocalBoard(owner: bigint): Uint8Array {
  const fbb = new flatbuffers.Builder();
  fbb.finish(
    LocalBoard.createLocalBoard(fbb, owner, false, false, false, false),
  );
  return fbb.asUint8Array();
}

function genThread(
  date: Date,
  author: bigint,
  board: bigint,
  instance?: { id: bigint; domain: string },
): { id: bigint; data: Uint8Array } {
  let title: string,
    content_url: string | undefined,
    content_text: string[] = [];
  if (faker.datatype.boolean(0.7)) {
    const link = genWikipediaLink();
    title = link.title;
    content_url = link.url;
    if (faker.datatype.boolean(0.25)) {
      content_text = faker.lorem.paragraph({ min: 1, max: 3 }).split("\n\n");
    }
  } else {
    title = faker.hacker.phrase();
    content_text = faker.lorem.paragraphs({ min: 1, max: 5 }).split("\n\n");
  }
  const fbb = new flatbuffers.Builder(),
    id = nextId++;
  fbb.finish(
    Thread.createThread(
      fbb,
      author,
      board,
      fbb.createString(title),
      BigInt(date.valueOf()) / 1000n,
      faker.helpers.maybe(
        () =>
          BigInt(
            faker.date.between({ from: date, to: Date() }).valueOf(),
          ) / 1000n,
        { probability: 0.1 },
      ) ?? null,
      instance ? instance.id : null,
      instance
        ? fbb.createString(
          `https://${instance.domain}/ap/activity/${id.toString(16)}`,
        )
        : 0,
      fbb.createString(
        `https://${instance ? instance.domain : ludwigDomain}/post/${
          id.toString(16)
        }`,
      ),
      content_url ? fbb.createString(content_url) : 0,
      content_text ? fbb.createString(content_text.join("\n\n")) : 0,
      Thread.createContentTextTypeVector(
        fbb,
        content_text.map(() => TextBlock.P),
      ),
      Thread.createContentTextVector(
        fbb,
        content_text.map((t) =>
          TextSpans.createTextSpans(
            fbb,
            TextSpans.createSpansTypeVector(fbb, [TextSpan.Plain]),
            TextSpans.createSpansVector(fbb, [fbb.createString(t)]),
          )
        ),
      ),
      faker.datatype.boolean(0.05)
        ? fbb.createString(faker.company.catchPhrase())
        : 0,
      faker.helpers.maybe(() => faker.helpers.enumValue(ModState), {
        probability: 0.05,
      }) ?? ModState.Visible,
      0,
    ),
  );
  return { id, data: fbb.asUint8Array() };
}

function genComment(
  parent: bigint,
  date: Date,
  author: bigint,
  thread: bigint,
  instance?: { id: bigint; domain: string },
): { id: bigint; data: Uint8Array } {
  const fbb = new flatbuffers.Builder(),
    id = nextId++,
    contentRaw = faker.lorem
      [faker.datatype.boolean() ? "paragraph" : "paragraphs"]({
        min: 1,
        max: 5,
      }),
    content = contentRaw.split("\n\n");
  fbb.finish(
    Comment.createComment(
      fbb,
      author,
      parent,
      thread,
      BigInt(date.valueOf()) / 1000n,
      faker.helpers.maybe(
        () =>
          BigInt(
            faker.date.between({ from: date, to: Date() }).valueOf(),
          ) / 1000n,
        { probability: 0.1 },
      ) ?? null,
      instance ? instance.id : null,
      instance
        ? fbb.createString(
          `https://${instance.domain}/ap/activity/${id.toString(16)}`,
        )
        : 0,
      fbb.createString(
        `https://${instance ? instance.domain : ludwigDomain}/post/${
          id.toString(16)
        }`,
      ),
      fbb.createSharedString(contentRaw),
      Comment.createContentTypeVector(
        fbb,
        content.map(() => TextBlock.P),
      ),
      Comment.createContentVector(
        fbb,
        content.map((t) =>
          TextSpans.createTextSpans(
            fbb,
            TextSpans.createSpansTypeVector(fbb, [TextSpan.Plain]),
            TextSpans.createSpansVector(fbb, [fbb.createString(t)]),
          )
        ),
      ),
      faker.datatype.boolean(0.05)
        ? fbb.createString(faker.company.catchPhrase())
        : 0,
      faker.helpers.maybe(() => faker.helpers.enumValue(ModState), {
        probability: 0.05,
      }) ?? ModState.Visible,
      0,
    ),
  );
  return { id, data: fbb.asUint8Array() };
}

async function genAll(scale = SCALE) {
  const instances = faker.helpers.multiple(genInstance, {
      count: Math.ceil(2 + Math.log10(scale)),
    }),
    usernames = faker.helpers.uniqueArray(
      faker.internet.domainWord,
      scale * 10,
    ),
    boardNames = faker.helpers.uniqueArray(
      faker.word.noun,
      Math.ceil(5 + Math.log10(scale)),
    ),
    localUsers: bigint[] = [],
    users: { id: bigint; instance?: { id: bigint; domain: string } }[] = [],
    localBoardCount = faker.number.int({
      min: boardNames.length / 2,
      max: boardNames.length - 1,
    }),
    localBoards: bigint[] = [],
    boards: bigint[] = [],
    postDates = faker.date.betweens({
      from: faker.date.recent({ days: 14 }),
      to: Date(),
      count: scale * 100,
    }),
    fbb = new flatbuffers.Builder(),
    gz = new GzEncoder(),
    write = (id: bigint, type: DumpType, data: Uint8Array) => {
      fbb.finishSizePrefixed(
        Dump.createDump(fbb, id, type, Dump.createDataVector(fbb, data)),
      );
      gz.write(new Foras.Memory(fbb.asUint8Array()));
      fbb.clear();
    },
    writeSettingInt = (key: string, val: bigint) => {
      const fbb = new flatbuffers.Builder();
      fbb.finish(
        SettingRecord.createSettingRecord(
          fbb,
          fbb.createString(key),
          val,
          0,
        ),
      );
      write(0n, DumpType.SettingRecord, fbb.asUint8Array());
    },
    writeSettingStr = (key: string, val: string | Uint8Array) => {
      const fbb = new flatbuffers.Builder();
      fbb.finish(
        SettingRecord.createSettingRecord(
          fbb,
          fbb.createString(key),
          null,
          fbb.createString(val),
        ),
      );
      write(0n, DumpType.SettingRecord, fbb.asUint8Array());
    };

  writeSettingStr("jwt_secret", crypto.getRandomValues(new Uint8Array(64)));
  writeSettingStr("domain", ludwigDomain);
  const now = BigInt(Date.now()) / 1000n;
  writeSettingInt("created_at", now);
  writeSettingInt("updated_at", now);
  writeSettingStr("name", "Ludwig");
  writeSettingStr("description", "Randomly Generated Ludwig Server");
  writeSettingInt("post_max_length", 1024n * 1024n);
  writeSettingInt("media_upload_enabled", 0n);
  writeSettingInt("board_creation_admin_only", 1n);
  writeSettingInt("federation_enabled", 0n);
  writeSettingInt("javascript_enabled", 1n);
  writeSettingInt("infinite_scroll_enabled", 1n);
  writeSettingInt("registration_enabled", 1n);
  writeSettingInt("registration_application_required", 1n);
  writeSettingInt("registration_invite_required", 0n);

  const admin = genUser("admin");
  write(admin.id, DumpType.User, admin.data);
  write(admin.id, DumpType.LocalUser, genLocalUser("admin", true));
  localUsers.push(admin.id);
  users.push({ id: admin.id });
  for (const name of usernames.slice(0, scale)) {
    const { id, data } = genUser(name);
    write(id, DumpType.User, data);
    write(id, DumpType.LocalUser, genLocalUser(name));
    localUsers.push(id);
    users.push({ id });
  }
  for (const name of usernames.slice(scale)) {
    const instance = faker.helpers.arrayElement(instances);
    const { id, data } = genUser(name, instance);
    write(id, DumpType.User, data);
    users.push({ id, instance });
  }
  for (const name of boardNames.slice(0, localBoardCount)) {
    const { id, data } = genBoard(name);
    write(id, DumpType.Board, data);
    write(
      id,
      DumpType.LocalBoard,
      genLocalBoard(faker.helpers.arrayElement(localUsers)),
    );
    localBoards.push(id);
    boards.push(id);
  }
  for (const name of boardNames.slice(localBoardCount)) {
    const { id, data } = genBoard(name, faker.helpers.arrayElement(instances));
    write(id, DumpType.Board, data);
    boards.push(id);
  }
  const posts: [bigint, bigint][] = postDates.slice(scale).reduce(
    (posts, date) => {
      const user = faker.helpers.arrayElement(users);
      if (faker.datatype.boolean()) {
        const { id, data } = genThread(
          date,
          user.id,
          faker.helpers.arrayElement(boards),
          user.instance,
        );
        write(id, DumpType.Thread, data);
        return [...posts, [id, id]];
      } else {
        const [parent, thread] = faker.helpers.arrayElement(posts),
          { id, data } = genComment(
            parent,
            date,
            user.id,
            thread,
            user.instance,
          );
        write(id, DumpType.Comment, data);
        return [...posts, [id, thread]];
      }
    },
    postDates.slice(0, scale).map((date): [bigint, bigint] => {
      const user = faker.helpers.arrayElement(users),
        { id, data } = genThread(
          date,
          user.id,
          faker.helpers.arrayElement(boards),
          user.instance,
        );
      write(id, DumpType.Thread, data);
      return [id, id];
    }),
  );

  writeSettingInt("next_id", BigInt(nextId));

  for (const [post] of posts) {
    const totalVotes = faker.number.int({ min: 0, max: users.length }),
      voters = faker.helpers.arrayElements(users, totalVotes),
      fbb = new flatbuffers.Builder();
    for (const { id: user } of voters) {
      fbb.clear();
      fbb.finish(VoteRecord.createVoteRecord(
        fbb,
        post,
        faker.helpers.arrayElement([
          Vote.Upvote,
          Vote.Upvote,
          Vote.Upvote,
          Vote.Upvote,
          Vote.Downvote,
        ]),
      ));
      write(user, DumpType.VoteRecord, fbb.asUint8Array());
    }
  }

  gz.flush();
  await Deno.stdout.write(gz.finish().copyAndDispose());
}

await genAll();
