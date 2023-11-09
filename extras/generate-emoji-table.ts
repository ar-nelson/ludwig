import flatbuffers from "npm:flatbuffers";
import * as path from "https://deno.land/std/path/mod.ts";
import { EmojiShortcode, EmojiTable } from "./flatbuffers/ludwig.ts";

const response = await fetch(
  "https://raw.githubusercontent.com/milesj/emojibase/master/packages/data/en/shortcodes/github.raw.json",
);
const json: { [emoji: string]: string | readonly string[] } = await response
  .json();
console.log(`json size: ${JSON.stringify(json).length}`);

const fbb = new flatbuffers.Builder(0);
fbb.finish(EmojiTable.createEmojiTable(
  fbb,
  EmojiTable.createEntriesVector(
    fbb,
    Object.entries(json).map(([codepoints, shortcodes]) => {
      const emoji = String.fromCodePoint(
        ...codepoints.split("-").map((hex) => parseInt(hex, 16)),
      );
      return EmojiShortcode.createEmojiShortcode(
        fbb,
        fbb.createString(emoji),
        EmojiShortcode.createShortcodesVector(
          fbb,
          (Array.isArray(shortcodes) ? shortcodes : [shortcodes]).map((s) =>
            fbb.createString(s)
          ),
        ),
      );
    }),
  ),
));

console.log(`fb size: ${fbb.asUint8Array().byteLength}`);

await Deno.writeFile(
  path.join(
    path.fromFileUrl(import.meta.url),
    "..",
    "..",
    "src",
    "static",
    "emoji_table.fb",
  ),
  fbb.asUint8Array(),
);
