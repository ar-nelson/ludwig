// automatically generated by the FlatBuffers compiler, do not modify

/* eslint-disable @typescript-eslint/no-unused-vars, @typescript-eslint/no-explicit-any, @typescript-eslint/no-non-null-assertion */

import * as flatbuffers from 'npm:flatbuffers';

export class SubscriptionBatch {
  bb: flatbuffers.ByteBuffer|null = null;
  bb_pos = 0;
  __init(i:number, bb:flatbuffers.ByteBuffer):SubscriptionBatch {
  this.bb_pos = i;
  this.bb = bb;
  return this;
}

static getRootAsSubscriptionBatch(bb:flatbuffers.ByteBuffer, obj?:SubscriptionBatch):SubscriptionBatch {
  return (obj || new SubscriptionBatch()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

static getSizePrefixedRootAsSubscriptionBatch(bb:flatbuffers.ByteBuffer, obj?:SubscriptionBatch):SubscriptionBatch {
  bb.setPosition(bb.position() + flatbuffers.SIZE_PREFIX_LENGTH);
  return (obj || new SubscriptionBatch()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

boards(index: number):bigint|null {
  const offset = this.bb!.__offset(this.bb_pos, 4);
  return offset ? this.bb!.readUint64(this.bb!.__vector(this.bb_pos + offset) + index * 8) : BigInt(0);
}

boardsLength():number {
  const offset = this.bb!.__offset(this.bb_pos, 4);
  return offset ? this.bb!.__vector_len(this.bb_pos + offset) : 0;
}

static startSubscriptionBatch(builder:flatbuffers.Builder) {
  builder.startObject(1);
}

static addBoards(builder:flatbuffers.Builder, boardsOffset:flatbuffers.Offset) {
  builder.addFieldOffset(0, boardsOffset, 0);
}

static createBoardsVector(builder:flatbuffers.Builder, data:bigint[]):flatbuffers.Offset {
  builder.startVector(8, data.length, 8);
  for (let i = data.length - 1; i >= 0; i--) {
    builder.addInt64(data[i]!);
  }
  return builder.endVector();
}

static startBoardsVector(builder:flatbuffers.Builder, numElems:number) {
  builder.startVector(8, numElems, 8);
}

static endSubscriptionBatch(builder:flatbuffers.Builder):flatbuffers.Offset {
  const offset = builder.endObject();
  return offset;
}

static createSubscriptionBatch(builder:flatbuffers.Builder, boardsOffset:flatbuffers.Offset):flatbuffers.Offset {
  SubscriptionBatch.startSubscriptionBatch(builder);
  SubscriptionBatch.addBoards(builder, boardsOffset);
  return SubscriptionBatch.endSubscriptionBatch(builder);
}
}