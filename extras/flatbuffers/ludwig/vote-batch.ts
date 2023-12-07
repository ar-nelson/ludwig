// automatically generated by the FlatBuffers compiler, do not modify

/* eslint-disable @typescript-eslint/no-unused-vars, @typescript-eslint/no-explicit-any, @typescript-eslint/no-non-null-assertion */

import * as flatbuffers from 'npm:flatbuffers';

export class VoteBatch {
  bb: flatbuffers.ByteBuffer|null = null;
  bb_pos = 0;
  __init(i:number, bb:flatbuffers.ByteBuffer):VoteBatch {
  this.bb_pos = i;
  this.bb = bb;
  return this;
}

static getRootAsVoteBatch(bb:flatbuffers.ByteBuffer, obj?:VoteBatch):VoteBatch {
  return (obj || new VoteBatch()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

static getSizePrefixedRootAsVoteBatch(bb:flatbuffers.ByteBuffer, obj?:VoteBatch):VoteBatch {
  bb.setPosition(bb.position() + flatbuffers.SIZE_PREFIX_LENGTH);
  return (obj || new VoteBatch()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

posts(index: number):bigint|null {
  const offset = this.bb!.__offset(this.bb_pos, 4);
  return offset ? this.bb!.readUint64(this.bb!.__vector(this.bb_pos + offset) + index * 8) : BigInt(0);
}

postsLength():number {
  const offset = this.bb!.__offset(this.bb_pos, 4);
  return offset ? this.bb!.__vector_len(this.bb_pos + offset) : 0;
}

static startVoteBatch(builder:flatbuffers.Builder) {
  builder.startObject(1);
}

static addPosts(builder:flatbuffers.Builder, postsOffset:flatbuffers.Offset) {
  builder.addFieldOffset(0, postsOffset, 0);
}

static createPostsVector(builder:flatbuffers.Builder, data:bigint[]):flatbuffers.Offset {
  builder.startVector(8, data.length, 8);
  for (let i = data.length - 1; i >= 0; i--) {
    builder.addInt64(data[i]!);
  }
  return builder.endVector();
}

static startPostsVector(builder:flatbuffers.Builder, numElems:number) {
  builder.startVector(8, numElems, 8);
}

static endVoteBatch(builder:flatbuffers.Builder):flatbuffers.Offset {
  const offset = builder.endObject();
  return offset;
}

static createVoteBatch(builder:flatbuffers.Builder, postsOffset:flatbuffers.Offset):flatbuffers.Offset {
  VoteBatch.startVoteBatch(builder);
  VoteBatch.addPosts(builder, postsOffset);
  return VoteBatch.endVoteBatch(builder);
}
}
