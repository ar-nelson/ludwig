// automatically generated by the FlatBuffers compiler, do not modify

/* eslint-disable @typescript-eslint/no-unused-vars, @typescript-eslint/no-explicit-any, @typescript-eslint/no-non-null-assertion */

import * as flatbuffers from 'npm:flatbuffers';

import { CommentSortType } from '../ludwig/comment-sort-type.ts';
import { ModState } from '../ludwig/mod-state.ts';
import { PlainTextWithEmojis, unionToPlainTextWithEmojis, unionListToPlainTextWithEmojis } from '../ludwig/plain-text-with-emojis.ts';
import { SortType } from '../ludwig/sort-type.ts';
import { TextBlock, unionToTextBlock, unionListToTextBlock } from '../ludwig/text-block.ts';


export class Board {
  bb: flatbuffers.ByteBuffer|null = null;
  bb_pos = 0;
  __init(i:number, bb:flatbuffers.ByteBuffer):Board {
  this.bb_pos = i;
  this.bb = bb;
  return this;
}

static getRootAsBoard(bb:flatbuffers.ByteBuffer, obj?:Board):Board {
  return (obj || new Board()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

static getSizePrefixedRootAsBoard(bb:flatbuffers.ByteBuffer, obj?:Board):Board {
  bb.setPosition(bb.position() + flatbuffers.SIZE_PREFIX_LENGTH);
  return (obj || new Board()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
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

actorId():string|null
actorId(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
actorId(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 10);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

inboxUrl():string|null
inboxUrl(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
inboxUrl(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 12);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

followersUrl():string|null
followersUrl(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
followersUrl(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 14);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

instance():bigint {
  const offset = this.bb!.__offset(this.bb_pos, 16);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : BigInt('0');
}

createdAt():bigint {
  const offset = this.bb!.__offset(this.bb_pos, 18);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : BigInt('0');
}

updatedAt():bigint|null {
  const offset = this.bb!.__offset(this.bb_pos, 20);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : null;
}

fetchedAt():bigint|null {
  const offset = this.bb!.__offset(this.bb_pos, 22);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : null;
}

deletedAt():bigint|null {
  const offset = this.bb!.__offset(this.bb_pos, 24);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : null;
}

descriptionRaw():string|null
descriptionRaw(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
descriptionRaw(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 26);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

descriptionType(index: number):TextBlock|null {
  const offset = this.bb!.__offset(this.bb_pos, 28);
  return offset ? this.bb!.readUint8(this.bb!.__vector(this.bb_pos + offset) + index) : 0;
}

descriptionTypeLength():number {
  const offset = this.bb!.__offset(this.bb_pos, 28);
  return offset ? this.bb!.__vector_len(this.bb_pos + offset) : 0;
}

descriptionTypeArray():Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 28);
  return offset ? new Uint8Array(this.bb!.bytes().buffer, this.bb!.bytes().byteOffset + this.bb!.__vector(this.bb_pos + offset), this.bb!.__vector_len(this.bb_pos + offset)) : null;
}

description(index: number, obj:any|string):any|string|null {
  const offset = this.bb!.__offset(this.bb_pos, 30);
  return offset ? this.bb!.__union_with_string(obj, this.bb!.__vector(this.bb_pos + offset) + index * 4) : null;
}

descriptionLength():number {
  const offset = this.bb!.__offset(this.bb_pos, 30);
  return offset ? this.bb!.__vector_len(this.bb_pos + offset) : 0;
}

iconUrl():string|null
iconUrl(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
iconUrl(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 32);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

bannerUrl():string|null
bannerUrl(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
bannerUrl(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 34);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

contentWarning():string|null
contentWarning(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
contentWarning(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 36);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

restrictedPosting():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 38);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : false;
}

approveSubscribe():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 40);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : false;
}

canUpvote():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 42);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : true;
}

canDownvote():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 44);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : true;
}

defaultSortType():SortType {
  const offset = this.bb!.__offset(this.bb_pos, 46);
  return offset ? this.bb!.readUint8(this.bb_pos + offset) : SortType.Active;
}

defaultCommentSortType():CommentSortType {
  const offset = this.bb!.__offset(this.bb_pos, 48);
  return offset ? this.bb!.readUint8(this.bb_pos + offset) : CommentSortType.Hot;
}

modState():ModState {
  const offset = this.bb!.__offset(this.bb_pos, 50);
  return offset ? this.bb!.readUint8(this.bb_pos + offset) : ModState.Visible;
}

modReason():string|null
modReason(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
modReason(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 52);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

static startBoard(builder:flatbuffers.Builder) {
  builder.startObject(25);
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

static addActorId(builder:flatbuffers.Builder, actorIdOffset:flatbuffers.Offset) {
  builder.addFieldOffset(3, actorIdOffset, 0);
}

static addInboxUrl(builder:flatbuffers.Builder, inboxUrlOffset:flatbuffers.Offset) {
  builder.addFieldOffset(4, inboxUrlOffset, 0);
}

static addFollowersUrl(builder:flatbuffers.Builder, followersUrlOffset:flatbuffers.Offset) {
  builder.addFieldOffset(5, followersUrlOffset, 0);
}

static addInstance(builder:flatbuffers.Builder, instance:bigint) {
  builder.addFieldInt64(6, instance, BigInt('0'));
}

static addCreatedAt(builder:flatbuffers.Builder, createdAt:bigint) {
  builder.addFieldInt64(7, createdAt, BigInt('0'));
}

static addUpdatedAt(builder:flatbuffers.Builder, updatedAt:bigint) {
  builder.addFieldInt64(8, updatedAt, BigInt(0));
}

static addFetchedAt(builder:flatbuffers.Builder, fetchedAt:bigint) {
  builder.addFieldInt64(9, fetchedAt, BigInt(0));
}

static addDeletedAt(builder:flatbuffers.Builder, deletedAt:bigint) {
  builder.addFieldInt64(10, deletedAt, BigInt(0));
}

static addDescriptionRaw(builder:flatbuffers.Builder, descriptionRawOffset:flatbuffers.Offset) {
  builder.addFieldOffset(11, descriptionRawOffset, 0);
}

static addDescriptionType(builder:flatbuffers.Builder, descriptionTypeOffset:flatbuffers.Offset) {
  builder.addFieldOffset(12, descriptionTypeOffset, 0);
}

static createDescriptionTypeVector(builder:flatbuffers.Builder, data:TextBlock[]):flatbuffers.Offset {
  builder.startVector(1, data.length, 1);
  for (let i = data.length - 1; i >= 0; i--) {
    builder.addInt8(data[i]!);
  }
  return builder.endVector();
}

static startDescriptionTypeVector(builder:flatbuffers.Builder, numElems:number) {
  builder.startVector(1, numElems, 1);
}

static addDescription(builder:flatbuffers.Builder, descriptionOffset:flatbuffers.Offset) {
  builder.addFieldOffset(13, descriptionOffset, 0);
}

static createDescriptionVector(builder:flatbuffers.Builder, data:flatbuffers.Offset[]):flatbuffers.Offset {
  builder.startVector(4, data.length, 4);
  for (let i = data.length - 1; i >= 0; i--) {
    builder.addOffset(data[i]!);
  }
  return builder.endVector();
}

static startDescriptionVector(builder:flatbuffers.Builder, numElems:number) {
  builder.startVector(4, numElems, 4);
}

static addIconUrl(builder:flatbuffers.Builder, iconUrlOffset:flatbuffers.Offset) {
  builder.addFieldOffset(14, iconUrlOffset, 0);
}

static addBannerUrl(builder:flatbuffers.Builder, bannerUrlOffset:flatbuffers.Offset) {
  builder.addFieldOffset(15, bannerUrlOffset, 0);
}

static addContentWarning(builder:flatbuffers.Builder, contentWarningOffset:flatbuffers.Offset) {
  builder.addFieldOffset(16, contentWarningOffset, 0);
}

static addRestrictedPosting(builder:flatbuffers.Builder, restrictedPosting:boolean) {
  builder.addFieldInt8(17, +restrictedPosting, +false);
}

static addApproveSubscribe(builder:flatbuffers.Builder, approveSubscribe:boolean) {
  builder.addFieldInt8(18, +approveSubscribe, +false);
}

static addCanUpvote(builder:flatbuffers.Builder, canUpvote:boolean) {
  builder.addFieldInt8(19, +canUpvote, +true);
}

static addCanDownvote(builder:flatbuffers.Builder, canDownvote:boolean) {
  builder.addFieldInt8(20, +canDownvote, +true);
}

static addDefaultSortType(builder:flatbuffers.Builder, defaultSortType:SortType) {
  builder.addFieldInt8(21, defaultSortType, SortType.Active);
}

static addDefaultCommentSortType(builder:flatbuffers.Builder, defaultCommentSortType:CommentSortType) {
  builder.addFieldInt8(22, defaultCommentSortType, CommentSortType.Hot);
}

static addModState(builder:flatbuffers.Builder, modState:ModState) {
  builder.addFieldInt8(23, modState, ModState.Visible);
}

static addModReason(builder:flatbuffers.Builder, modReasonOffset:flatbuffers.Offset) {
  builder.addFieldOffset(24, modReasonOffset, 0);
}

static endBoard(builder:flatbuffers.Builder):flatbuffers.Offset {
  const offset = builder.endObject();
  builder.requiredField(offset, 4) // name
  return offset;
}

static createBoard(builder:flatbuffers.Builder, nameOffset:flatbuffers.Offset, displayNameTypeOffset:flatbuffers.Offset, displayNameOffset:flatbuffers.Offset, actorIdOffset:flatbuffers.Offset, inboxUrlOffset:flatbuffers.Offset, followersUrlOffset:flatbuffers.Offset, instance:bigint, createdAt:bigint, updatedAt:bigint|null, fetchedAt:bigint|null, deletedAt:bigint|null, descriptionRawOffset:flatbuffers.Offset, descriptionTypeOffset:flatbuffers.Offset, descriptionOffset:flatbuffers.Offset, iconUrlOffset:flatbuffers.Offset, bannerUrlOffset:flatbuffers.Offset, contentWarningOffset:flatbuffers.Offset, restrictedPosting:boolean, approveSubscribe:boolean, canUpvote:boolean, canDownvote:boolean, defaultSortType:SortType, defaultCommentSortType:CommentSortType, modState:ModState, modReasonOffset:flatbuffers.Offset):flatbuffers.Offset {
  Board.startBoard(builder);
  Board.addName(builder, nameOffset);
  Board.addDisplayNameType(builder, displayNameTypeOffset);
  Board.addDisplayName(builder, displayNameOffset);
  Board.addActorId(builder, actorIdOffset);
  Board.addInboxUrl(builder, inboxUrlOffset);
  Board.addFollowersUrl(builder, followersUrlOffset);
  Board.addInstance(builder, instance);
  Board.addCreatedAt(builder, createdAt);
  if (updatedAt !== null)
    Board.addUpdatedAt(builder, updatedAt);
  if (fetchedAt !== null)
    Board.addFetchedAt(builder, fetchedAt);
  if (deletedAt !== null)
    Board.addDeletedAt(builder, deletedAt);
  Board.addDescriptionRaw(builder, descriptionRawOffset);
  Board.addDescriptionType(builder, descriptionTypeOffset);
  Board.addDescription(builder, descriptionOffset);
  Board.addIconUrl(builder, iconUrlOffset);
  Board.addBannerUrl(builder, bannerUrlOffset);
  Board.addContentWarning(builder, contentWarningOffset);
  Board.addRestrictedPosting(builder, restrictedPosting);
  Board.addApproveSubscribe(builder, approveSubscribe);
  Board.addCanUpvote(builder, canUpvote);
  Board.addCanDownvote(builder, canDownvote);
  Board.addDefaultSortType(builder, defaultSortType);
  Board.addDefaultCommentSortType(builder, defaultCommentSortType);
  Board.addModState(builder, modState);
  Board.addModReason(builder, modReasonOffset);
  return Board.endBoard(builder);
}
}
