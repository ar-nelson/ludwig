// A Deno script to generate a Ludwig database dump with a huge amount of
// realistic mock data.

import { writeAllSync } from "https://deno.land/std@0.208.0/streams/mod.ts";
import { pbkdf2Sync } from "node:crypto";
import { faker } from "npm:@faker-js/faker";
import * as flatbuffers from "npm:flatbuffers";
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
  RichText,
  Salt,
  SettingRecord,
  SortType,
  SubscriptionBatch,
  Thread,
  User,
  VoteBatch,
} from "./flatbuffers/ludwig.ts";

const ludwigDomain = "http://localhost:2023";
const PASSWORD_SALT = new TextEncoder().encode("0123456789abcdef");

const SCALE = 100;
let nextId = 0x10n;

function genInstance(): { id: bigint; domain: string } {
  return { id: nextId++, domain: `${faker.internet.domainWord()}.test` };
}

function randomModState(fbb: flatbuffers.Builder): [ModState, number] {
  const mod_state = faker.helpers.maybe(
    () => faker.helpers.arrayElement([ModState.Flagged, ModState.Locked, ModState.Removed]),
    { probability: 0.05 }
  ) ?? ModState.Normal;
  return [
    mod_state,
    mod_state == ModState.Normal ? faker.helpers.maybe(() => fbb.createString(faker.company.catchPhrase())) ?? 0 : 0
  ]
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
        displayName ? [RichText.Text] : [],
      ),
      User.createDisplayNameVector(
        fbb,
        displayName ? [fbb.createString(displayName)] : [],
      ),
      bio ? fbb.createSharedString(bio) : 0,
      User.createBioTypeVector(fbb, bio ? [RichText.Text] : []),
      User.createBioVector(
        fbb,
        bio ? [fbb.createSharedString(bio)] : [],
      ),
      instance
        ? fbb.createString(`https://${instance.domain}/u/${baseName}`)
        : 0,
      instance
        ? fbb.createString(
          `https://${instance.domain}/ap/actor/${baseName}/inbox`,
        )
        : 0,
      0,
      instance ? instance.id : 0n,
      BigInt(faker.date.past().valueOf()) / 1000n,
      null,
      null,
      null,
      faker.number.int({ min: 0, max: 0xffffffff }),
      avatar ? fbb.createString(avatar) : 0,
      banner ? fbb.createString(banner) : 0,
      faker.datatype.boolean(0.05),
      ...randomModState(fbb)
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
        displayName ? [RichText.Text] : [],
      ),
      Board.createDisplayNameVector(
        fbb,
        displayName ? [fbb.createString(displayName)] : [],
      ),
      instance
        ? fbb.createString(`https://${instance.domain}/c/${baseName}`)
        : 0,
      instance
        ? fbb.createString(
          `https://${instance.domain}/ap/actor/${baseName}/inbox`,
        )
        : 0,
      instance
        ? fbb.createString(
          `https://${instance.domain}/ap/actor/${baseName}/followers`,
        )
        : 0,
      instance ? instance.id : 0n,
      BigInt(faker.date.past().valueOf()) / 1000n,
      null,
      null,
      null,
      description ? fbb.createSharedString(description) : 0,
      Board.createDescriptionTypeVector(
        fbb,
        description ? [RichText.Text] : [],
      ),
      Board.createDescriptionVector(
        fbb,
        description ? [fbb.createSharedString(description)] : []
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
      ...randomModState(fbb)
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
      Thread.createTitleTypeVector(fbb, [RichText.Text]),
      Thread.createTitleVector(fbb, [fbb.createString(title)]),
      BigInt(date.valueOf()) / 1000n,
      faker.helpers.maybe(
        () =>
          BigInt(
            faker.date.between({ from: date, to: Date() }).valueOf(),
          ) / 1000n,
        { probability: 0.1 },
      ) ?? null,
      null,
      null,
      instance ? instance.id : 0n,
      faker.number.int({ min: 0, max: 0xffffffff }),
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
      Thread.createContentTextTypeVector(fbb, [RichText.Text]),
      Thread.createContentTextVector(
        fbb,
        [fbb.createString(content_text.map((t) => `<p>${t}</p>`).join(""))]
      ),
      faker.datatype.boolean(0.05)
        ? fbb.createString(faker.company.catchPhrase())
        : 0,
      false,
      ...randomModState(fbb),
      ModState.Normal,
      0
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
      null,
      null,
      instance ? instance.id : 0n,
      faker.number.int({ min: 0, max: 0xffffffff }),
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
      Comment.createContentTypeVector(fbb, [RichText.Text]),
      Comment.createContentVector(
        fbb,
        [fbb.createString(content.map((t) => `<p>${t}</p>`).join(""))]
      ),
      faker.datatype.boolean(0.05)
        ? fbb.createString(faker.company.catchPhrase())
        : 0,
      ...randomModState(fbb),
      ModState.Normal,
      0
    ),
  );
  return { id, data: fbb.asUint8Array() };
}

function genAll(scale = SCALE) {
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
    write = (id: bigint, type: DumpType, data: Uint8Array) => {
      const fbb = new flatbuffers.Builder();
      fbb.finishSizePrefixed(
        Dump.createDump(fbb, id, type, Dump.createDataVector(fbb, data)),
      );
      writeAllSync(Deno.stdout, fbb.asUint8Array());
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
    };

  // const admin = genUser("admin");
  // write(admin.id, DumpType.User, admin.data);
  // write(admin.id, DumpType.LocalUser, genLocalUser("admin", true));
  // localUsers.push(admin.id);
  // users.push({ id: admin.id });
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
  const BATCH_SIZE = 1024;

  for (const { id: user } of users) {
    const fbb = new flatbuffers.Builder(),
      totalVotes = faker.number.int({
        min: Math.floor(posts.length * 0.3),
        max: Math.ceil(posts.length * 0.9),
      }),
      votedPosts = faker.helpers.arrayElements(posts, totalVotes).map((x) =>
        x[0]
      ),
      splitAt = Math.floor(votedPosts.length * 0.8),
      upvoted = votedPosts.slice(0, splitAt).sort((a, b) => Number(a - b)),
      downvoted = votedPosts.slice(splitAt).sort((a, b) => Number(a - b));
    for (let i = 0; i < upvoted.length; i += BATCH_SIZE) {
      fbb.clear();
      fbb.finish(
        VoteBatch.createVoteBatch(
          fbb,
          VoteBatch.createPostsVector(
            fbb,
            upvoted.slice(i, Math.min(i + BATCH_SIZE, upvoted.length)),
          ),
        ),
      );
      write(user, DumpType.UpvoteBatch, fbb.asUint8Array());
    }
    for (let i = 0; i < downvoted.length; i += BATCH_SIZE) {
      fbb.clear();
      fbb.finish(
        VoteBatch.createVoteBatch(
          fbb,
          VoteBatch.createPostsVector(
            fbb,
            downvoted.slice(i, Math.min(i + BATCH_SIZE, downvoted.length)),
          ),
        ),
      );
      write(user, DumpType.DownvoteBatch, fbb.asUint8Array());
    }
  }
  for (const user of localUsers) {
    const fbb = new flatbuffers.Builder(),
      subCount = faker.number.int({ min: 2, max: boards.length * 0.75 }),
      subs = faker.helpers.arrayElements(boards, subCount);
    fbb.finish(
      SubscriptionBatch.createSubscriptionBatch(
        fbb,
        SubscriptionBatch.createBoardsVector(fbb, subs),
      ),
    );
    write(user, DumpType.SubscriptionBatch, fbb.asUint8Array());
  }
}

genAll();
