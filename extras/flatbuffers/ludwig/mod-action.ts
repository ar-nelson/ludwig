// automatically generated by the FlatBuffers compiler, do not modify

/* eslint-disable @typescript-eslint/no-unused-vars, @typescript-eslint/no-explicit-any, @typescript-eslint/no-non-null-assertion */

import * as flatbuffers from 'npm:flatbuffers';

import { ModActionType } from '../ludwig/mod-action-type.ts';


export class ModAction {
  bb: flatbuffers.ByteBuffer|null = null;
  bb_pos = 0;
  __init(i:number, bb:flatbuffers.ByteBuffer):ModAction {
  this.bb_pos = i;
  this.bb = bb;
  return this;
}

static getRootAsModAction(bb:flatbuffers.ByteBuffer, obj?:ModAction):ModAction {
  return (obj || new ModAction()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

static getSizePrefixedRootAsModAction(bb:flatbuffers.ByteBuffer, obj?:ModAction):ModAction {
  bb.setPosition(bb.position() + flatbuffers.SIZE_PREFIX_LENGTH);
  return (obj || new ModAction()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

type():ModActionType {
  const offset = this.bb!.__offset(this.bb_pos, 4);
  return offset ? this.bb!.readInt8(this.bb_pos + offset) : ModActionType.EditPost;
}

author():bigint {
  const offset = this.bb!.__offset(this.bb_pos, 6);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : BigInt('0');
}

target():bigint {
  const offset = this.bb!.__offset(this.bb_pos, 8);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : BigInt('0');
}

content():string|null
content(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
content(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 10);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

static startModAction(builder:flatbuffers.Builder) {
  builder.startObject(4);
}

static addType(builder:flatbuffers.Builder, type:ModActionType) {
  builder.addFieldInt8(0, type, ModActionType.EditPost);
}

static addAuthor(builder:flatbuffers.Builder, author:bigint) {
  builder.addFieldInt64(1, author, BigInt('0'));
}

static addTarget(builder:flatbuffers.Builder, target:bigint) {
  builder.addFieldInt64(2, target, BigInt('0'));
}

static addContent(builder:flatbuffers.Builder, contentOffset:flatbuffers.Offset) {
  builder.addFieldOffset(3, contentOffset, 0);
}

static endModAction(builder:flatbuffers.Builder):flatbuffers.Offset {
  const offset = builder.endObject();
  return offset;
}

static createModAction(builder:flatbuffers.Builder, type:ModActionType, author:bigint, target:bigint, contentOffset:flatbuffers.Offset):flatbuffers.Offset {
  ModAction.startModAction(builder);
  ModAction.addType(builder, type);
  ModAction.addAuthor(builder, author);
  ModAction.addTarget(builder, target);
  ModAction.addContent(builder, contentOffset);
  return ModAction.endModAction(builder);
}
}
