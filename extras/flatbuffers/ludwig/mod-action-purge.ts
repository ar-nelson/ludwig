// automatically generated by the FlatBuffers compiler, do not modify

/* eslint-disable @typescript-eslint/no-unused-vars, @typescript-eslint/no-explicit-any, @typescript-eslint/no-non-null-assertion */

import * as flatbuffers from 'npm:flatbuffers';

export class ModActionPurge {
  bb: flatbuffers.ByteBuffer|null = null;
  bb_pos = 0;
  __init(i:number, bb:flatbuffers.ByteBuffer):ModActionPurge {
  this.bb_pos = i;
  this.bb = bb;
  return this;
}

static getRootAsModActionPurge(bb:flatbuffers.ByteBuffer, obj?:ModActionPurge):ModActionPurge {
  return (obj || new ModActionPurge()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

static getSizePrefixedRootAsModActionPurge(bb:flatbuffers.ByteBuffer, obj?:ModActionPurge):ModActionPurge {
  bb.setPosition(bb.position() + flatbuffers.SIZE_PREFIX_LENGTH);
  return (obj || new ModActionPurge()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

alsoPurged(index: number):bigint|null {
  const offset = this.bb!.__offset(this.bb_pos, 4);
  return offset ? this.bb!.readUint64(this.bb!.__vector(this.bb_pos + offset) + index * 8) : BigInt(0);
}

alsoPurgedLength():number {
  const offset = this.bb!.__offset(this.bb_pos, 4);
  return offset ? this.bb!.__vector_len(this.bb_pos + offset) : 0;
}

static startModActionPurge(builder:flatbuffers.Builder) {
  builder.startObject(1);
}

static addAlsoPurged(builder:flatbuffers.Builder, alsoPurgedOffset:flatbuffers.Offset) {
  builder.addFieldOffset(0, alsoPurgedOffset, 0);
}

static createAlsoPurgedVector(builder:flatbuffers.Builder, data:bigint[]):flatbuffers.Offset {
  builder.startVector(8, data.length, 8);
  for (let i = data.length - 1; i >= 0; i--) {
    builder.addInt64(data[i]!);
  }
  return builder.endVector();
}

static startAlsoPurgedVector(builder:flatbuffers.Builder, numElems:number) {
  builder.startVector(8, numElems, 8);
}

static endModActionPurge(builder:flatbuffers.Builder):flatbuffers.Offset {
  const offset = builder.endObject();
  return offset;
}

static createModActionPurge(builder:flatbuffers.Builder, alsoPurgedOffset:flatbuffers.Offset):flatbuffers.Offset {
  ModActionPurge.startModActionPurge(builder);
  ModActionPurge.addAlsoPurged(builder, alsoPurgedOffset);
  return ModActionPurge.endModActionPurge(builder);
}
}
