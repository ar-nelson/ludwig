// automatically generated by the FlatBuffers compiler, do not modify

/* eslint-disable @typescript-eslint/no-unused-vars, @typescript-eslint/no-explicit-any, @typescript-eslint/no-non-null-assertion */

import * as flatbuffers from 'npm:flatbuffers';

export class LocalBoard {
  bb: flatbuffers.ByteBuffer|null = null;
  bb_pos = 0;
  __init(i:number, bb:flatbuffers.ByteBuffer):LocalBoard {
  this.bb_pos = i;
  this.bb = bb;
  return this;
}

static getRootAsLocalBoard(bb:flatbuffers.ByteBuffer, obj?:LocalBoard):LocalBoard {
  return (obj || new LocalBoard()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

static getSizePrefixedRootAsLocalBoard(bb:flatbuffers.ByteBuffer, obj?:LocalBoard):LocalBoard {
  bb.setPosition(bb.position() + flatbuffers.SIZE_PREFIX_LENGTH);
  return (obj || new LocalBoard()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

owner():bigint {
  const offset = this.bb!.__offset(this.bb_pos, 4);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : BigInt('0');
}

federated():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 6);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : true;
}

private_():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 8);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : false;
}

inviteRequired():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 10);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : false;
}

inviteModOnly():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 12);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : false;
}

static startLocalBoard(builder:flatbuffers.Builder) {
  builder.startObject(5);
}

static addOwner(builder:flatbuffers.Builder, owner:bigint) {
  builder.addFieldInt64(0, owner, BigInt('0'));
}

static addFederated(builder:flatbuffers.Builder, federated:boolean) {
  builder.addFieldInt8(1, +federated, +true);
}

static addPrivate(builder:flatbuffers.Builder, private_:boolean) {
  builder.addFieldInt8(2, +private_, +false);
}

static addInviteRequired(builder:flatbuffers.Builder, inviteRequired:boolean) {
  builder.addFieldInt8(3, +inviteRequired, +false);
}

static addInviteModOnly(builder:flatbuffers.Builder, inviteModOnly:boolean) {
  builder.addFieldInt8(4, +inviteModOnly, +false);
}

static endLocalBoard(builder:flatbuffers.Builder):flatbuffers.Offset {
  const offset = builder.endObject();
  return offset;
}

static createLocalBoard(builder:flatbuffers.Builder, owner:bigint, federated:boolean, private_:boolean, inviteRequired:boolean, inviteModOnly:boolean):flatbuffers.Offset {
  LocalBoard.startLocalBoard(builder);
  LocalBoard.addOwner(builder, owner);
  LocalBoard.addFederated(builder, federated);
  LocalBoard.addPrivate(builder, private_);
  LocalBoard.addInviteRequired(builder, inviteRequired);
  LocalBoard.addInviteModOnly(builder, inviteModOnly);
  return LocalBoard.endLocalBoard(builder);
}
}
