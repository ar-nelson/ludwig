// automatically generated by the FlatBuffers compiler, do not modify

/* eslint-disable @typescript-eslint/no-unused-vars, @typescript-eslint/no-explicit-any, @typescript-eslint/no-non-null-assertion */

import * as flatbuffers from 'npm:flatbuffers';

export class TextCodeBlock {
  bb: flatbuffers.ByteBuffer|null = null;
  bb_pos = 0;
  __init(i:number, bb:flatbuffers.ByteBuffer):TextCodeBlock {
  this.bb_pos = i;
  this.bb = bb;
  return this;
}

static getRootAsTextCodeBlock(bb:flatbuffers.ByteBuffer, obj?:TextCodeBlock):TextCodeBlock {
  return (obj || new TextCodeBlock()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

static getSizePrefixedRootAsTextCodeBlock(bb:flatbuffers.ByteBuffer, obj?:TextCodeBlock):TextCodeBlock {
  bb.setPosition(bb.position() + flatbuffers.SIZE_PREFIX_LENGTH);
  return (obj || new TextCodeBlock()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

text():string|null
text(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
text(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 4);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

language():string|null
language(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
language(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 6);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

static startTextCodeBlock(builder:flatbuffers.Builder) {
  builder.startObject(2);
}

static addText(builder:flatbuffers.Builder, textOffset:flatbuffers.Offset) {
  builder.addFieldOffset(0, textOffset, 0);
}

static addLanguage(builder:flatbuffers.Builder, languageOffset:flatbuffers.Offset) {
  builder.addFieldOffset(1, languageOffset, 0);
}

static endTextCodeBlock(builder:flatbuffers.Builder):flatbuffers.Offset {
  const offset = builder.endObject();
  builder.requiredField(offset, 4) // text
  return offset;
}

static createTextCodeBlock(builder:flatbuffers.Builder, textOffset:flatbuffers.Offset, languageOffset:flatbuffers.Offset):flatbuffers.Offset {
  TextCodeBlock.startTextCodeBlock(builder);
  TextCodeBlock.addText(builder, textOffset);
  TextCodeBlock.addLanguage(builder, languageOffset);
  return TextCodeBlock.endTextCodeBlock(builder);
}
}
