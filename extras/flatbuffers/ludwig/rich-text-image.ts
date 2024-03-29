// automatically generated by the FlatBuffers compiler, do not modify

/* eslint-disable @typescript-eslint/no-unused-vars, @typescript-eslint/no-explicit-any, @typescript-eslint/no-non-null-assertion */

import * as flatbuffers from 'npm:flatbuffers';

export class RichTextImage {
  bb: flatbuffers.ByteBuffer|null = null;
  bb_pos = 0;
  __init(i:number, bb:flatbuffers.ByteBuffer):RichTextImage {
  this.bb_pos = i;
  this.bb = bb;
  return this;
}

static getRootAsRichTextImage(bb:flatbuffers.ByteBuffer, obj?:RichTextImage):RichTextImage {
  return (obj || new RichTextImage()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

static getSizePrefixedRootAsRichTextImage(bb:flatbuffers.ByteBuffer, obj?:RichTextImage):RichTextImage {
  bb.setPosition(bb.position() + flatbuffers.SIZE_PREFIX_LENGTH);
  return (obj || new RichTextImage()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

src():string|null
src(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
src(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 4);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

alt():string|null
alt(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
alt(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 6);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

static startRichTextImage(builder:flatbuffers.Builder) {
  builder.startObject(2);
}

static addSrc(builder:flatbuffers.Builder, srcOffset:flatbuffers.Offset) {
  builder.addFieldOffset(0, srcOffset, 0);
}

static addAlt(builder:flatbuffers.Builder, altOffset:flatbuffers.Offset) {
  builder.addFieldOffset(1, altOffset, 0);
}

static endRichTextImage(builder:flatbuffers.Builder):flatbuffers.Offset {
  const offset = builder.endObject();
  builder.requiredField(offset, 4) // src
  return offset;
}

static createRichTextImage(builder:flatbuffers.Builder, srcOffset:flatbuffers.Offset, altOffset:flatbuffers.Offset):flatbuffers.Offset {
  RichTextImage.startRichTextImage(builder);
  RichTextImage.addSrc(builder, srcOffset);
  RichTextImage.addAlt(builder, altOffset);
  return RichTextImage.endRichTextImage(builder);
}
}
