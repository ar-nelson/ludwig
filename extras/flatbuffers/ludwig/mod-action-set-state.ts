// automatically generated by the FlatBuffers compiler, do not modify

/* eslint-disable @typescript-eslint/no-unused-vars, @typescript-eslint/no-explicit-any, @typescript-eslint/no-non-null-assertion */

import * as flatbuffers from 'npm:flatbuffers';

import { ModState } from '../ludwig/mod-state.ts';


export class ModActionSetState {
  bb: flatbuffers.ByteBuffer|null = null;
  bb_pos = 0;
  __init(i:number, bb:flatbuffers.ByteBuffer):ModActionSetState {
  this.bb_pos = i;
  this.bb = bb;
  return this;
}

static getRootAsModActionSetState(bb:flatbuffers.ByteBuffer, obj?:ModActionSetState):ModActionSetState {
  return (obj || new ModActionSetState()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

static getSizePrefixedRootAsModActionSetState(bb:flatbuffers.ByteBuffer, obj?:ModActionSetState):ModActionSetState {
  bb.setPosition(bb.position() + flatbuffers.SIZE_PREFIX_LENGTH);
  return (obj || new ModActionSetState()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

oldValue():ModState {
  const offset = this.bb!.__offset(this.bb_pos, 4);
  return offset ? this.bb!.readUint8(this.bb_pos + offset) : ModState.Normal;
}

newValue():ModState {
  const offset = this.bb!.__offset(this.bb_pos, 6);
  return offset ? this.bb!.readUint8(this.bb_pos + offset) : ModState.Normal;
}

static startModActionSetState(builder:flatbuffers.Builder) {
  builder.startObject(2);
}

static addOldValue(builder:flatbuffers.Builder, oldValue:ModState) {
  builder.addFieldInt8(0, oldValue, ModState.Normal);
}

static addNewValue(builder:flatbuffers.Builder, newValue:ModState) {
  builder.addFieldInt8(1, newValue, ModState.Normal);
}

static endModActionSetState(builder:flatbuffers.Builder):flatbuffers.Offset {
  const offset = builder.endObject();
  return offset;
}

static createModActionSetState(builder:flatbuffers.Builder, oldValue:ModState, newValue:ModState):flatbuffers.Offset {
  ModActionSetState.startModActionSetState(builder);
  ModActionSetState.addOldValue(builder, oldValue);
  ModActionSetState.addNewValue(builder, newValue);
  return ModActionSetState.endModActionSetState(builder);
}
}