// A Deno script to generate a Ludwig database dump with a huge amount of
// realistic mock data.

import { faker } from "npm:@faker-js/faker";
import { decode as hexDecode } from "https://deno.land/std/encoding/hex.ts";
import { encode as base64Encode } from "https://deno.land/std/encoding/base64.ts";

// The default password is "fhqwhgads", for every account.
const PASSWORD_SALT = new TextEncoder().encode("0123456789abcdef");
const PASSWORD_HASH = hexDecode(
  new TextEncoder().encode(
    "1fbf4b3d9639fc815fb394b95ac2913d14e0f9375a5a7e6fa6291ab660b9d9e6",
  ),
);

const SCALE = 100;
let nextId = 0;
let ludwigDomain = "localhost:2023";

function genInstance(): { id: number; domain: string } {
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
  instance?: { id: number; domain: string },
): { id: number; user: User } {
  return {
    id: nextId++,
    user: {
      name: instance ? `${baseName}@${instance.domain}` : baseName,
      display_name: faker.helpers.maybe(() => faker.person.fullName(), {
        probability: 0.5,
      }),
      bio_safe: faker.helpers.maybe(() => faker.person.bio(), {
        probability: 0.75,
      }),
      actor_id: instance && `https://${instance.domain}/ap/actor/${baseName}`,
      inbox_url: instance &&
        `https://${instance.domain}/ap/actor/${baseName}/inbox`,
      instance: instance?.id,
      created_at: Math.floor(faker.date.past().valueOf() / 1000),
      avatar_url: faker.helpers.maybe(
        () => genImageUrl(faker.number.int({ min: 32, max: 512 })),
        { probability: 0.75 },
      ),
      banner_url: faker.helpers.maybe(() => {
        const w = faker.number.int({ min: 320, max: 1920 }),
          h = faker.number.int({ min: 100, max: Math.min(w, 720) });
        return genImageUrl(w, h);
      }, { probability: 0.5 }),
      bot: faker.datatype.boolean(0.05),
      mod_state: faker.helpers.maybe(() => faker.helpers.enumValue(ModState), {
        probability: 0.05,
      }),
    },
  };
}

function genLocalUser(
  baseName: string,
  isAdmin = false,
): { id: number; user: User; localUser: LocalUser } {
  const { id, user } = genUser(baseName);
  return {
    id,
    user,
    localUser: {
      email: faker.internet.email(),
      password_hash: { bytes: [...PASSWORD_HASH] },
      password_salt: { bytes: [...PASSWORD_SALT] },
      show_avatars: faker.datatype.boolean(0.8),
      show_karma: faker.datatype.boolean(0.8),
      hide_cw_posts: faker.datatype.boolean(0.5),
      admin: isAdmin,
    },
  };
}

function genBoard(
  baseName: string,
  instance?: { id: number; domain: string },
): { id: number; board: Board } {
  return {
    id: nextId++,
    board: {
      name: instance ? `${baseName}@${instance.domain}` : baseName,
      display_name: faker.helpers.maybe(
        () => baseName[0].toUpperCase() + baseName.slice(1),
        { probability: 0.75 },
      ),
      actor_id: instance && `https://${instance.domain}/ap/actor/${baseName}`,
      inbox_url: instance &&
        `https://${instance.domain}/ap/actor/${baseName}/inbox`,
      instance: instance?.id,
      created_at: Math.floor(faker.date.past().valueOf() / 1000),
      description_safe: faker.helpers.maybe(
        () => faker.lorem.paragraphs({ min: 1, max: 3 }),
        { probability: 0.75 },
      ),
      icon_url: faker.helpers.maybe(
        () =>
          genImageUrl(
            faker.number.int({ min: 32, max: 512 }),
            undefined,
            baseName,
          ),
        { probability: 0.75 },
      ),
      banner_url: faker.helpers.maybe(() => {
        const w = faker.number.int({ min: 320, max: 1920 }),
          h = faker.number.int({ min: 100, max: Math.min(w, 720) });
        return genImageUrl(w, h, baseName);
      }, { probability: 0.5 }),
      content_warning: faker.datatype.boolean(0.1)
        ? faker.company.catchPhrase()
        : undefined,
    },
  };
}

function genLocalBoard(
  baseName: string,
  owner: number,
): { id: number; board: Board; localBoard: LocalBoard } {
  const { id, board } = genBoard(baseName);
  return { id, board, localBoard: { owner } };
}

function genThread(
  date: Date,
  author: number,
  board: number,
  instance?: { id: number; domain: string },
): { id: number; thread: Thread } {
  let title: string,
    content_url: string | undefined,
    content_text_safe: string | undefined;
  if (faker.datatype.boolean(0.7)) {
    const link = genWikipediaLink();
    title = link.title;
    content_url = link.url;
    if (faker.datatype.boolean(0.25)) {
      content_text_safe = faker.lorem.paragraph({ min: 1, max: 3 });
    }
  } else {
    title = faker.hacker.phrase();
    content_text_safe = faker.lorem.paragraphs({ min: 1, max: 5 });
  }
  const id = nextId++;
  return {
    id,
    thread: {
      author,
      board,
      title,
      created_at: Math.floor(date.valueOf() / 1000),
      updated_at: faker.helpers.maybe(
        () =>
          Math.floor(
            faker.date.between({ from: date, to: Date() }).valueOf() / 1000,
          ),
        { probability: 0.1 },
      ),
      instance: instance?.id,
      activity_url: instance &&
        `https://${instance.domain}/ap/activity/${id.toString(16)}`,
      original_post_url: `https://${
        instance ? instance.domain : ludwigDomain
      }/post/${id.toString(16)}`,
      content_url,
      content_text_safe,
      content_warning: faker.datatype.boolean(0.05)
        ? faker.company.catchPhrase()
        : undefined,
      mod_state: faker.helpers.maybe(() => faker.helpers.enumValue(ModState), {
        probability: 0.05,
      }),
    },
  };
}

function genComment(
  parent: number,
  date: Date,
  author: number,
  thread: number,
  instance?: { id: number; domain: string },
): { id: number; comment: Comment } {
  const id = nextId++;
  return {
    id,
    comment: {
      author,
      parent,
      thread,
      created_at: Math.floor(date.valueOf() / 1000),
      updated_at: faker.helpers.maybe(
        () =>
          Math.floor(
            faker.date.between({ from: date, to: Date() }).valueOf() / 1000,
          ),
        { probability: 0.1 },
      ),
      instance: instance?.id,
      activity_url: instance &&
        `https://${instance.domain}/ap/activity/${id.toString(16)}`,
      original_post_url: `https://${
        instance ? instance.domain : ludwigDomain
      }/post/${id.toString(16)}`,
      content_safe: faker.lorem
        [faker.datatype.boolean() ? "paragraph" : "paragraphs"]({
          min: 1,
          max: 5,
        }),
      mod_state: faker.helpers.maybe(() => faker.helpers.enumValue(ModState), {
        probability: 0.05,
      }),
    },
  };
}

enum ModState {
  Visible = 0,
  Flagged = 1,
  Locked = 2,
  Hidden = 3,
  Removed = 4,
}

interface User {
  name: string;
  display_name?: string;
  bio_raw?: string;
  bio_safe?: string;
  actor_id?: string;
  inbox_url?: string;
  instance?: number;
  created_at: number;
  updated_at?: number;
  deleted_at?: number;
  avatar_url?: string;
  banner_url?: string;
  bot?: boolean;
  mod_state?: ModState;
}

interface LocalUser {
  email?: string;
  password_hash?: { bytes: number[] };
  password_salt?: { bytes: number[] };
  admin?: boolean;
  accepted_application?: boolean;
  email_verified?: boolean;
  open_links_in_new_tab?: boolean;
  send_notifications_to_email?: boolean;
  show_avatars?: boolean;
  show_bot_accounts?: boolean;
  show_new_post_notifs?: boolean;
  hide_cw_posts?: boolean;
  expand_cw_posts?: boolean;
  expand_cw_images?: boolean;
  show_read_posts?: boolean;
  show_karma?: boolean;
  interface_language?: string;
  theme?: string;
  default_listing_type?: number;
  default_sort_type?: number;
}

interface Board {
  name: string;
  display_name?: string;
  actor_id?: string;
  inbox_url?: string;
  instance?: number;
  created_at: number;
  updated_at?: number;
  description_raw?: string;
  description_safe?: string;
  icon_url?: string;
  banner_url?: string;
  content_warning?: string;
  restricted_posting?: boolean;
  can_upvote?: boolean;
  can_downvote?: boolean;
  invite_only?: boolean;
  mod_state?: ModState;
}

interface LocalBoard {
  owner: number;
  federated?: boolean;
  private?: boolean;
}

interface Thread {
  author: number;
  board: number;
  title: string;
  created_at: number;
  updated_at?: number;
  instance?: number;
  activity_url?: string;
  original_post_url: string;
  content_url?: string;
  content_text_raw?: string;
  content_text_safe?: string;
  content_warning?: string;
  mod_state?: ModState;
}

interface Comment {
  author: number;
  parent: number;
  thread: number;
  created_at: number;
  updated_at?: number;
  instance?: number;
  activity_url?: string;
  original_post_url: string;
  content_raw?: string;
  content_safe: string;
  content_warning?: string;
  mod_state?: ModState;
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
    localUsers = [
      genLocalUser("admin", true),
      ...usernames.slice(0, scale).map((name) => genLocalUser(name)),
    ],
    users = [
      ...localUsers,
      ...usernames.slice(scale).map((name) =>
        genUser(name, faker.helpers.arrayElement(instances))
      ),
    ],
    localBoardCount = faker.number.int({
      min: boardNames.length / 2,
      max: boardNames.length - 1,
    }),
    localBoards = boardNames.slice(0, localBoardCount).map((name) =>
      genLocalBoard(name, faker.helpers.arrayElement(localUsers).id)
    ),
    boards = [
      ...localBoards,
      ...boardNames.slice(localBoardCount).map((name) =>
        genBoard(name, faker.helpers.arrayElement(instances))
      ),
    ],
    postDates = faker.date.betweens({
      from: faker.date.recent({ days: 14 }),
      to: Date(),
      count: scale * 100,
    }),
    { threads, comments } = postDates.slice(scale).reduce(
      ({ threads, comments }, date) => {
        const user = faker.helpers.arrayElement(users);
        const instance = instances.find((i) => i.id === user.user.instance);
        if (faker.datatype.boolean()) {
          return {
            threads: [
              ...threads,
              genThread(
                date,
                user.id,
                faker.helpers.arrayElement(boards).id,
                instance,
              ),
            ],
            comments,
          };
        } else {
          const parent = faker.helpers.arrayElement([...threads, ...comments]);
          return {
            threads,
            comments: [
              ...comments,
              genComment(
                parent.id,
                date,
                user.id,
                "thread" in parent ? parent.id : parent.comment.thread,
                instance,
              ),
            ],
          };
        }
      },
      {
        threads: postDates.slice(0, scale).map((date) => {
          const user = faker.helpers.arrayElement(users);
          return genThread(
            date,
            user.id,
            faker.helpers.arrayElement(boards).id,
            instances.find((i) => i.id === user.user.instance),
          );
        }),
        comments: [] as { id: number; comment: Comment }[],
      },
    );

  function uint64Base64(uint: bigint): string {
    return base64Encode(BigUint64Array.of(uint).buffer);
  }

  console.log(`S next_id ${uint64Base64(BigInt(nextId))}`);
  console.log(
    `S hash_seed ${base64Encode(crypto.getRandomValues(new Uint8Array(8)))}`,
  );
  console.log(
    `S jwt_secret ${base64Encode(crypto.getRandomValues(new Uint8Array(64)))}`,
  );
  console.log(`S domain ${base64Encode(ludwigDomain)}`);
  const now = uint64Base64(BigInt(Date.now()) / 1000n);
  console.log(`S created_at ${now}`);
  console.log(`S updated_at ${now}`);
  console.log(`S name ${base64Encode("Ludwig")}`);
  console.log(
    `S description ${base64Encode("Randomly Generated Ludwig Server")}`,
  );
  console.log(`S post_max_length ${uint64Base64(1024n * 1024n)}`);
  console.log(`S media_upload_enabled ${uint64Base64(0n)}`);
  console.log(`S board_creation_admin_only ${uint64Base64(1n)}`);
  console.log(`S federation_enabled ${uint64Base64(0n)}`);

  for (const u of users) {
    console.log(`u ${u.id} ${JSON.stringify(u.user)}`);
  }
  for (const u of localUsers) {
    console.log(`U ${u.id} ${JSON.stringify(u.localUser)}`);
  }
  for (const b of boards) {
    console.log(`b ${b.id} ${JSON.stringify(b.board)}`);
  }
  for (const b of localBoards) {
    console.log(`B ${b.id} ${JSON.stringify(b.localBoard)}`);
  }
  for (const t of threads) {
    console.log(`t ${t.id} ${JSON.stringify(t.thread)}`);
  }
  for (const c of comments) {
    console.log(`c ${c.id} ${JSON.stringify(c.comment)}`);
  }
  for (
    const post of [
      ...threads.map((p) => p.id),
      ...comments.map((n) => n.id),
    ]
  ) {
    const totalVotes = faker.number.int({ min: 0, max: users.length }),
      voters = faker.helpers.arrayElements(users, totalVotes);
    for (const { id: user } of voters) {
      console.log(
        `v ${user} ${post} ${faker.helpers.arrayElement([1, 1, 1, 1, -1])}`,
      );
    }
  }
}

genAll();
