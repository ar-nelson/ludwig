// automatically generated by the FlatBuffers compiler, do not modify

/* eslint-disable @typescript-eslint/no-unused-vars, @typescript-eslint/no-explicit-any, @typescript-eslint/no-non-null-assertion */

import * as flatbuffers from 'npm:flatbuffers';

import { ModState } from '../ludwig/mod-state.ts';
import { PlainTextWithEmojis, unionToPlainTextWithEmojis, unionListToPlainTextWithEmojis } from '../ludwig/plain-text-with-emojis.ts';
import { TextBlock, unionToTextBlock, unionListToTextBlock } from '../ludwig/text-block.ts';


export class User {
  bb: flatbuffers.ByteBuffer|null = null;
  bb_pos = 0;
  __init(i:number, bb:flatbuffers.ByteBuffer):User {
  this.bb_pos = i;
  this.bb = bb;
  return this;
}

static getRootAsUser(bb:flatbuffers.ByteBuffer, obj?:User):User {
  return (obj || new User()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

static getSizePrefixedRootAsUser(bb:flatbuffers.ByteBuffer, obj?:User):User {
  bb.setPosition(bb.position() + flatbuffers.SIZE_PREFIX_LENGTH);
  return (obj || new User()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

name():string|null
name(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
name(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 4);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

displayNameType(index: number):PlainTextWithEmojis|null {
  const offset = this.bb!.__offset(this.bb_pos, 6);
  return offset ? this.bb!.readUint8(this.bb!.__vector(this.bb_pos + offset) + index) : 0;
}

displayNameTypeLength():number {
  const offset = this.bb!.__offset(this.bb_pos, 6);
  return offset ? this.bb!.__vector_len(this.bb_pos + offset) : 0;
}

displayNameTypeArray():Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 6);
  return offset ? new Uint8Array(this.bb!.bytes().buffer, this.bb!.bytes().byteOffset + this.bb!.__vector(this.bb_pos + offset), this.bb!.__vector_len(this.bb_pos + offset)) : null;
}

displayName(index: number, obj:any|string):any|string|null {
  const offset = this.bb!.__offset(this.bb_pos, 8);
  return offset ? this.bb!.__union_with_string(obj, this.bb!.__vector(this.bb_pos + offset) + index * 4) : null;
}

displayNameLength():number {
  const offset = this.bb!.__offset(this.bb_pos, 8);
  return offset ? this.bb!.__vector_len(this.bb_pos + offset) : 0;
}

bioRaw():string|null
bioRaw(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
bioRaw(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 10);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

bioType(index: number):TextBlock|null {
  const offset = this.bb!.__offset(this.bb_pos, 12);
  return offset ? this.bb!.readUint8(this.bb!.__vector(this.bb_pos + offset) + index) : 0;
}

bioTypeLength():number {
  const offset = this.bb!.__offset(this.bb_pos, 12);
  return offset ? this.bb!.__vector_len(this.bb_pos + offset) : 0;
}

bioTypeArray():Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 12);
  return offset ? new Uint8Array(this.bb!.bytes().buffer, this.bb!.bytes().byteOffset + this.bb!.__vector(this.bb_pos + offset), this.bb!.__vector_len(this.bb_pos + offset)) : null;
}

bio(index: number, obj:any|string):any|string|null {
  const offset = this.bb!.__offset(this.bb_pos, 14);
  return offset ? this.bb!.__union_with_string(obj, this.bb!.__vector(this.bb_pos + offset) + index * 4) : null;
}

bioLength():number {
  const offset = this.bb!.__offset(this.bb_pos, 14);
  return offset ? this.bb!.__vector_len(this.bb_pos + offset) : 0;
}

actorId():string|null
actorId(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
actorId(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 16);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

inboxUrl():string|null
inboxUrl(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
inboxUrl(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 18);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

matrixUserId():string|null
matrixUserId(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
matrixUserId(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 20);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

instance():bigint {
  const offset = this.bb!.__offset(this.bb_pos, 22);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : BigInt('0');
}

createdAt():bigint {
  const offset = this.bb!.__offset(this.bb_pos, 24);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : BigInt('0');
}

updatedAt():bigint|null {
  const offset = this.bb!.__offset(this.bb_pos, 26);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : null;
}

fetchedAt():bigint|null {
  const offset = this.bb!.__offset(this.bb_pos, 28);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : null;
}

deletedAt():bigint|null {
  const offset = this.bb!.__offset(this.bb_pos, 30);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : null;
}

avatarUrl():string|null
avatarUrl(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
avatarUrl(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 32);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

bannerUrl():string|null
bannerUrl(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
bannerUrl(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 34);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

bot():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 36);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : false;
}

modState():ModState {
  const offset = this.bb!.__offset(this.bb_pos, 38);
  return offset ? this.bb!.readUint8(this.bb_pos + offset) : ModState.Visible;
}

modReason():string|null
modReason(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
modReason(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 40);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

static startUser(builder:flatbuffers.Builder) {
  builder.startObject(19);
}

static addName(builder:flatbuffers.Builder, nameOffset:flatbuffers.Offset) {
  builder.addFieldOffset(0, nameOffset, 0);
}

static addDisplayNameType(builder:flatbuffers.Builder, displayNameTypeOffset:flatbuffers.Offset) {
  builder.addFieldOffset(1, displayNameTypeOffset, 0);
}

static createDisplayNameTypeVector(builder:flatbuffers.Builder, data:PlainTextWithEmojis[]):flatbuffers.Offset {
  builder.startVector(1, data.length, 1);
  for (let i = data.length - 1; i >= 0; i--) {
    builder.addInt8(data[i]!);
  }
  return builder.endVector();
}

static startDisplayNameTypeVector(builder:flatbuffers.Builder, numElems:number) {
  builder.startVector(1, numElems, 1);
}

static addDisplayName(builder:flatbuffers.Builder, displayNameOffset:flatbuffers.Offset) {
  builder.addFieldOffset(2, displayNameOffset, 0);
}

static createDisplayNameVector(builder:flatbuffers.Builder, data:flatbuffers.Offset[]):flatbuffers.Offset {
  builder.startVector(4, data.length, 4);
  for (let i = data.length - 1; i >= 0; i--) {
    builder.addOffset(data[i]!);
  }
  return builder.endVector();
}

static startDisplayNameVector(builder:flatbuffers.Builder, numElems:number) {
  builder.startVector(4, numElems, 4);
}

static addBioRaw(builder:flatbuffers.Builder, bioRawOffset:flatbuffers.Offset) {
  builder.addFieldOffset(3, bioRawOffset, 0);
}

static addBioType(builder:flatbuffers.Builder, bioTypeOffset:flatbuffers.Offset) {
  builder.addFieldOffset(4, bioTypeOffset, 0);
}

static createBioTypeVector(builder:flatbuffers.Builder, data:TextBlock[]):flatbuffers.Offset {
  builder.startVector(1, data.length, 1);
  for (let i = data.length - 1; i >= 0; i--) {
    builder.addInt8(data[i]!);
  }
  return builder.endVector();
}

static startBioTypeVector(builder:flatbuffers.Builder, numElems:number) {
  builder.startVector(1, numElems, 1);
}

static addBio(builder:flatbuffers.Builder, bioOffset:flatbuffers.Offset) {
  builder.addFieldOffset(5, bioOffset, 0);
}

static createBioVector(builder:flatbuffers.Builder, data:flatbuffers.Offset[]):flatbuffers.Offset {
  builder.startVector(4, data.length, 4);
  for (let i = data.length - 1; i >= 0; i--) {
    builder.addOffset(data[i]!);
  }
  return builder.endVector();
}

static startBioVector(builder:flatbuffers.Builder, numElems:number) {
  builder.startVector(4, numElems, 4);
}

static addActorId(builder:flatbuffers.Builder, actorIdOffset:flatbuffers.Offset) {
  builder.addFieldOffset(6, actorIdOffset, 0);
}

static addInboxUrl(builder:flatbuffers.Builder, inboxUrlOffset:flatbuffers.Offset) {
  builder.addFieldOffset(7, inboxUrlOffset, 0);
}

static addMatrixUserId(builder:flatbuffers.Builder, matrixUserIdOffset:flatbuffers.Offset) {
  builder.addFieldOffset(8, matrixUserIdOffset, 0);
}

static addInstance(builder:flatbuffers.Builder, instance:bigint) {
  builder.addFieldInt64(9, instance, BigInt('0'));
}

static addCreatedAt(builder:flatbuffers.Builder, createdAt:bigint) {
  builder.addFieldInt64(10, createdAt, BigInt('0'));
}

static addUpdatedAt(builder:flatbuffers.Builder, updatedAt:bigint) {
  builder.addFieldInt64(11, updatedAt, BigInt(0));
}

static addFetchedAt(builder:flatbuffers.Builder, fetchedAt:bigint) {
  builder.addFieldInt64(12, fetchedAt, BigInt(0));
}

static addDeletedAt(builder:flatbuffers.Builder, deletedAt:bigint) {
  builder.addFieldInt64(13, deletedAt, BigInt(0));
}

static addAvatarUrl(builder:flatbuffers.Builder, avatarUrlOffset:flatbuffers.Offset) {
  builder.addFieldOffset(14, avatarUrlOffset, 0);
}

static addBannerUrl(builder:flatbuffers.Builder, bannerUrlOffset:flatbuffers.Offset) {
  builder.addFieldOffset(15, bannerUrlOffset, 0);
}

static addBot(builder:flatbuffers.Builder, bot:boolean) {
  builder.addFieldInt8(16, +bot, +false);
}

static addModState(builder:flatbuffers.Builder, modState:ModState) {
  builder.addFieldInt8(17, modState, ModState.Visible);
}

static addModReason(builder:flatbuffers.Builder, modReasonOffset:flatbuffers.Offset) {
  builder.addFieldOffset(18, modReasonOffset, 0);
}

static endUser(builder:flatbuffers.Builder):flatbuffers.Offset {
  const offset = builder.endObject();
  builder.requiredField(offset, 4) // name
  return offset;
}

static createUser(builder:flatbuffers.Builder, nameOffset:flatbuffers.Offset, displayNameTypeOffset:flatbuffers.Offset, displayNameOffset:flatbuffers.Offset, bioRawOffset:flatbuffers.Offset, bioTypeOffset:flatbuffers.Offset, bioOffset:flatbuffers.Offset, actorIdOffset:flatbuffers.Offset, inboxUrlOffset:flatbuffers.Offset, matrixUserIdOffset:flatbuffers.Offset, instance:bigint, createdAt:bigint, updatedAt:bigint|null, fetchedAt:bigint|null, deletedAt:bigint|null, avatarUrlOffset:flatbuffers.Offset, bannerUrlOffset:flatbuffers.Offset, bot:boolean, modState:ModState, modReasonOffset:flatbuffers.Offset):flatbuffers.Offset {
  User.startUser(builder);
  User.addName(builder, nameOffset);
  User.addDisplayNameType(builder, displayNameTypeOffset);
  User.addDisplayName(builder, displayNameOffset);
  User.addBioRaw(builder, bioRawOffset);
  User.addBioType(builder, bioTypeOffset);
  User.addBio(builder, bioOffset);
  User.addActorId(builder, actorIdOffset);
  User.addInboxUrl(builder, inboxUrlOffset);
  User.addMatrixUserId(builder, matrixUserIdOffset);
  User.addInstance(builder, instance);
  User.addCreatedAt(builder, createdAt);
  if (updatedAt !== null)
    User.addUpdatedAt(builder, updatedAt);
  if (fetchedAt !== null)
    User.addFetchedAt(builder, fetchedAt);
  if (deletedAt !== null)
    User.addDeletedAt(builder, deletedAt);
  User.addAvatarUrl(builder, avatarUrlOffset);
  User.addBannerUrl(builder, bannerUrlOffset);
  User.addBot(builder, bot);
  User.addModState(builder, modState);
  User.addModReason(builder, modReasonOffset);
  return User.endUser(builder);
}
}
