// automatically generated by the FlatBuffers compiler, do not modify

/* eslint-disable @typescript-eslint/no-unused-vars, @typescript-eslint/no-explicit-any, @typescript-eslint/no-non-null-assertion */

import * as flatbuffers from 'npm:flatbuffers';

import { ModState } from '../ludwig/mod-state.ts';
import { RichText, unionToRichText, unionListToRichText } from '../ludwig/rich-text.ts';


export class Comment {
  bb: flatbuffers.ByteBuffer|null = null;
  bb_pos = 0;
  __init(i:number, bb:flatbuffers.ByteBuffer):Comment {
  this.bb_pos = i;
  this.bb = bb;
  return this;
}

static getRootAsComment(bb:flatbuffers.ByteBuffer, obj?:Comment):Comment {
  return (obj || new Comment()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

static getSizePrefixedRootAsComment(bb:flatbuffers.ByteBuffer, obj?:Comment):Comment {
  bb.setPosition(bb.position() + flatbuffers.SIZE_PREFIX_LENGTH);
  return (obj || new Comment()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

author():bigint {
  const offset = this.bb!.__offset(this.bb_pos, 4);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : BigInt('0');
}

parent():bigint {
  const offset = this.bb!.__offset(this.bb_pos, 6);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : BigInt('0');
}

thread():bigint {
  const offset = this.bb!.__offset(this.bb_pos, 8);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : BigInt('0');
}

createdAt():bigint {
  const offset = this.bb!.__offset(this.bb_pos, 10);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : BigInt('0');
}

updatedAt():bigint|null {
  const offset = this.bb!.__offset(this.bb_pos, 12);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : null;
}

fetchedAt():bigint|null {
  const offset = this.bb!.__offset(this.bb_pos, 14);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : null;
}

deletedAt():bigint|null {
  const offset = this.bb!.__offset(this.bb_pos, 16);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : null;
}

instance():bigint {
  const offset = this.bb!.__offset(this.bb_pos, 18);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : BigInt('0');
}

salt():number {
  const offset = this.bb!.__offset(this.bb_pos, 20);
  return offset ? this.bb!.readUint32(this.bb_pos + offset) : 0;
}

activityUrl():string|null
activityUrl(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
activityUrl(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 22);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

originalPostUrl():string|null
originalPostUrl(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
originalPostUrl(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 24);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

contentRaw():string|null
contentRaw(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
contentRaw(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 26);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

contentType(index: number):RichText|null {
  const offset = this.bb!.__offset(this.bb_pos, 28);
  return offset ? this.bb!.readUint8(this.bb!.__vector(this.bb_pos + offset) + index) : 0;
}

contentTypeLength():number {
  const offset = this.bb!.__offset(this.bb_pos, 28);
  return offset ? this.bb!.__vector_len(this.bb_pos + offset) : 0;
}

contentTypeArray():Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 28);
  return offset ? new Uint8Array(this.bb!.bytes().buffer, this.bb!.bytes().byteOffset + this.bb!.__vector(this.bb_pos + offset), this.bb!.__vector_len(this.bb_pos + offset)) : null;
}

content(index: number, obj:any|string):any|string|null {
  const offset = this.bb!.__offset(this.bb_pos, 30);
  return offset ? this.bb!.__union_with_string(obj, this.bb!.__vector(this.bb_pos + offset) + index * 4) : null;
}

contentLength():number {
  const offset = this.bb!.__offset(this.bb_pos, 30);
  return offset ? this.bb!.__vector_len(this.bb_pos + offset) : 0;
}

contentWarning():string|null
contentWarning(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
contentWarning(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 32);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

modState():ModState {
  const offset = this.bb!.__offset(this.bb_pos, 34);
  return offset ? this.bb!.readUint8(this.bb_pos + offset) : ModState.Normal;
}

modReason():string|null
modReason(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
modReason(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 36);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

boardModState():ModState {
  const offset = this.bb!.__offset(this.bb_pos, 38);
  return offset ? this.bb!.readUint8(this.bb_pos + offset) : ModState.Normal;
}

boardModReason():string|null
boardModReason(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
boardModReason(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 40);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

static startComment(builder:flatbuffers.Builder) {
  builder.startObject(19);
}

static addAuthor(builder:flatbuffers.Builder, author:bigint) {
  builder.addFieldInt64(0, author, BigInt('0'));
}

static addParent(builder:flatbuffers.Builder, parent:bigint) {
  builder.addFieldInt64(1, parent, BigInt('0'));
}

static addThread(builder:flatbuffers.Builder, thread:bigint) {
  builder.addFieldInt64(2, thread, BigInt('0'));
}

static addCreatedAt(builder:flatbuffers.Builder, createdAt:bigint) {
  builder.addFieldInt64(3, createdAt, BigInt('0'));
}

static addUpdatedAt(builder:flatbuffers.Builder, updatedAt:bigint) {
  builder.addFieldInt64(4, updatedAt, BigInt(0));
}

static addFetchedAt(builder:flatbuffers.Builder, fetchedAt:bigint) {
  builder.addFieldInt64(5, fetchedAt, BigInt(0));
}

static addDeletedAt(builder:flatbuffers.Builder, deletedAt:bigint) {
  builder.addFieldInt64(6, deletedAt, BigInt(0));
}

static addInstance(builder:flatbuffers.Builder, instance:bigint) {
  builder.addFieldInt64(7, instance, BigInt('0'));
}

static addSalt(builder:flatbuffers.Builder, salt:number) {
  builder.addFieldInt32(8, salt, 0);
}

static addActivityUrl(builder:flatbuffers.Builder, activityUrlOffset:flatbuffers.Offset) {
  builder.addFieldOffset(9, activityUrlOffset, 0);
}

static addOriginalPostUrl(builder:flatbuffers.Builder, originalPostUrlOffset:flatbuffers.Offset) {
  builder.addFieldOffset(10, originalPostUrlOffset, 0);
}

static addContentRaw(builder:flatbuffers.Builder, contentRawOffset:flatbuffers.Offset) {
  builder.addFieldOffset(11, contentRawOffset, 0);
}

static addContentType(builder:flatbuffers.Builder, contentTypeOffset:flatbuffers.Offset) {
  builder.addFieldOffset(12, contentTypeOffset, 0);
}

static createContentTypeVector(builder:flatbuffers.Builder, data:RichText[]):flatbuffers.Offset {
  builder.startVector(1, data.length, 1);
  for (let i = data.length - 1; i >= 0; i--) {
    builder.addInt8(data[i]!);
  }
  return builder.endVector();
}

static startContentTypeVector(builder:flatbuffers.Builder, numElems:number) {
  builder.startVector(1, numElems, 1);
}

static addContent(builder:flatbuffers.Builder, contentOffset:flatbuffers.Offset) {
  builder.addFieldOffset(13, contentOffset, 0);
}

static createContentVector(builder:flatbuffers.Builder, data:flatbuffers.Offset[]):flatbuffers.Offset {
  builder.startVector(4, data.length, 4);
  for (let i = data.length - 1; i >= 0; i--) {
    builder.addOffset(data[i]!);
  }
  return builder.endVector();
}

static startContentVector(builder:flatbuffers.Builder, numElems:number) {
  builder.startVector(4, numElems, 4);
}

static addContentWarning(builder:flatbuffers.Builder, contentWarningOffset:flatbuffers.Offset) {
  builder.addFieldOffset(14, contentWarningOffset, 0);
}

static addModState(builder:flatbuffers.Builder, modState:ModState) {
  builder.addFieldInt8(15, modState, ModState.Normal);
}

static addModReason(builder:flatbuffers.Builder, modReasonOffset:flatbuffers.Offset) {
  builder.addFieldOffset(16, modReasonOffset, 0);
}

static addBoardModState(builder:flatbuffers.Builder, boardModState:ModState) {
  builder.addFieldInt8(17, boardModState, ModState.Normal);
}

static addBoardModReason(builder:flatbuffers.Builder, boardModReasonOffset:flatbuffers.Offset) {
  builder.addFieldOffset(18, boardModReasonOffset, 0);
}

static endComment(builder:flatbuffers.Builder):flatbuffers.Offset {
  const offset = builder.endObject();
  builder.requiredField(offset, 28) // content_type
  builder.requiredField(offset, 30) // content
  return offset;
}

static createComment(builder:flatbuffers.Builder, author:bigint, parent:bigint, thread:bigint, createdAt:bigint, updatedAt:bigint|null, fetchedAt:bigint|null, deletedAt:bigint|null, instance:bigint, salt:number, activityUrlOffset:flatbuffers.Offset, originalPostUrlOffset:flatbuffers.Offset, contentRawOffset:flatbuffers.Offset, contentTypeOffset:flatbuffers.Offset, contentOffset:flatbuffers.Offset, contentWarningOffset:flatbuffers.Offset, modState:ModState, modReasonOffset:flatbuffers.Offset, boardModState:ModState, boardModReasonOffset:flatbuffers.Offset):flatbuffers.Offset {
  Comment.startComment(builder);
  Comment.addAuthor(builder, author);
  Comment.addParent(builder, parent);
  Comment.addThread(builder, thread);
  Comment.addCreatedAt(builder, createdAt);
  if (updatedAt !== null)
    Comment.addUpdatedAt(builder, updatedAt);
  if (fetchedAt !== null)
    Comment.addFetchedAt(builder, fetchedAt);
  if (deletedAt !== null)
    Comment.addDeletedAt(builder, deletedAt);
  Comment.addInstance(builder, instance);
  Comment.addSalt(builder, salt);
  Comment.addActivityUrl(builder, activityUrlOffset);
  Comment.addOriginalPostUrl(builder, originalPostUrlOffset);
  Comment.addContentRaw(builder, contentRawOffset);
  Comment.addContentType(builder, contentTypeOffset);
  Comment.addContent(builder, contentOffset);
  Comment.addContentWarning(builder, contentWarningOffset);
  Comment.addModState(builder, modState);
  Comment.addModReason(builder, modReasonOffset);
  Comment.addBoardModState(builder, boardModState);
  Comment.addBoardModReason(builder, boardModReasonOffset);
  return Comment.endComment(builder);
}
}
