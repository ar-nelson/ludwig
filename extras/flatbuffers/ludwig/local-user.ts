// automatically generated by the FlatBuffers compiler, do not modify

/* eslint-disable @typescript-eslint/no-unused-vars, @typescript-eslint/no-explicit-any, @typescript-eslint/no-non-null-assertion */

import * as flatbuffers from 'npm:flatbuffers';

import { CommentSortType } from '../ludwig/comment-sort-type.ts';
import { Hash } from '../ludwig/hash.ts';
import { Salt } from '../ludwig/salt.ts';
import { SortType } from '../ludwig/sort-type.ts';


export class LocalUser {
  bb: flatbuffers.ByteBuffer|null = null;
  bb_pos = 0;
  __init(i:number, bb:flatbuffers.ByteBuffer):LocalUser {
  this.bb_pos = i;
  this.bb = bb;
  return this;
}

static getRootAsLocalUser(bb:flatbuffers.ByteBuffer, obj?:LocalUser):LocalUser {
  return (obj || new LocalUser()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

static getSizePrefixedRootAsLocalUser(bb:flatbuffers.ByteBuffer, obj?:LocalUser):LocalUser {
  bb.setPosition(bb.position() + flatbuffers.SIZE_PREFIX_LENGTH);
  return (obj || new LocalUser()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

email():string|null
email(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
email(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 4);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

passwordHash(obj?:Hash):Hash|null {
  const offset = this.bb!.__offset(this.bb_pos, 6);
  return offset ? (obj || new Hash()).__init(this.bb_pos + offset, this.bb!) : null;
}

passwordSalt(obj?:Salt):Salt|null {
  const offset = this.bb!.__offset(this.bb_pos, 8);
  return offset ? (obj || new Salt()).__init(this.bb_pos + offset, this.bb!) : null;
}

admin():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 10);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : false;
}

approved():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 12);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : false;
}

acceptedApplication():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 14);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : false;
}

emailVerified():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 16);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : false;
}

invite():bigint|null {
  const offset = this.bb!.__offset(this.bb_pos, 18);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : null;
}

openLinksInNewTab():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 20);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : false;
}

sendNotificationsToEmail():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 22);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : false;
}

showAvatars():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 24);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : true;
}

showImagesThreads():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 26);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : true;
}

showImagesComments():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 28);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : true;
}

showBotAccounts():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 30);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : true;
}

showNewPostNotifs():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 32);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : true;
}

hideCwPosts():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 34);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : false;
}

expandCwPosts():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 36);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : false;
}

expandCwImages():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 38);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : false;
}

showReadPosts():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 40);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : true;
}

showKarma():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 42);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : true;
}

javascriptEnabled():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 44);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : true;
}

infiniteScrollEnabled():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 46);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : true;
}

interfaceLanguage():string|null
interfaceLanguage(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
interfaceLanguage(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 48);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

theme():string|null
theme(optionalEncoding:flatbuffers.Encoding):string|Uint8Array|null
theme(optionalEncoding?:any):string|Uint8Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 50);
  return offset ? this.bb!.__string(this.bb_pos + offset, optionalEncoding) : null;
}

defaultSortType():SortType {
  const offset = this.bb!.__offset(this.bb_pos, 52);
  return offset ? this.bb!.readUint8(this.bb_pos + offset) : SortType.Active;
}

defaultCommentSortType():CommentSortType {
  const offset = this.bb!.__offset(this.bb_pos, 54);
  return offset ? this.bb!.readUint8(this.bb_pos + offset) : CommentSortType.Hot;
}

static startLocalUser(builder:flatbuffers.Builder) {
  builder.startObject(26);
}

static addEmail(builder:flatbuffers.Builder, emailOffset:flatbuffers.Offset) {
  builder.addFieldOffset(0, emailOffset, 0);
}

static addPasswordHash(builder:flatbuffers.Builder, passwordHashOffset:flatbuffers.Offset) {
  builder.addFieldStruct(1, passwordHashOffset, 0);
}

static addPasswordSalt(builder:flatbuffers.Builder, passwordSaltOffset:flatbuffers.Offset) {
  builder.addFieldStruct(2, passwordSaltOffset, 0);
}

static addAdmin(builder:flatbuffers.Builder, admin:boolean) {
  builder.addFieldInt8(3, +admin, +false);
}

static addApproved(builder:flatbuffers.Builder, approved:boolean) {
  builder.addFieldInt8(4, +approved, +false);
}

static addAcceptedApplication(builder:flatbuffers.Builder, acceptedApplication:boolean) {
  builder.addFieldInt8(5, +acceptedApplication, +false);
}

static addEmailVerified(builder:flatbuffers.Builder, emailVerified:boolean) {
  builder.addFieldInt8(6, +emailVerified, +false);
}

static addInvite(builder:flatbuffers.Builder, invite:bigint) {
  builder.addFieldInt64(7, invite, BigInt(0));
}

static addOpenLinksInNewTab(builder:flatbuffers.Builder, openLinksInNewTab:boolean) {
  builder.addFieldInt8(8, +openLinksInNewTab, +false);
}

static addSendNotificationsToEmail(builder:flatbuffers.Builder, sendNotificationsToEmail:boolean) {
  builder.addFieldInt8(9, +sendNotificationsToEmail, +false);
}

static addShowAvatars(builder:flatbuffers.Builder, showAvatars:boolean) {
  builder.addFieldInt8(10, +showAvatars, +true);
}

static addShowImagesThreads(builder:flatbuffers.Builder, showImagesThreads:boolean) {
  builder.addFieldInt8(11, +showImagesThreads, +true);
}

static addShowImagesComments(builder:flatbuffers.Builder, showImagesComments:boolean) {
  builder.addFieldInt8(12, +showImagesComments, +true);
}

static addShowBotAccounts(builder:flatbuffers.Builder, showBotAccounts:boolean) {
  builder.addFieldInt8(13, +showBotAccounts, +true);
}

static addShowNewPostNotifs(builder:flatbuffers.Builder, showNewPostNotifs:boolean) {
  builder.addFieldInt8(14, +showNewPostNotifs, +true);
}

static addHideCwPosts(builder:flatbuffers.Builder, hideCwPosts:boolean) {
  builder.addFieldInt8(15, +hideCwPosts, +false);
}

static addExpandCwPosts(builder:flatbuffers.Builder, expandCwPosts:boolean) {
  builder.addFieldInt8(16, +expandCwPosts, +false);
}

static addExpandCwImages(builder:flatbuffers.Builder, expandCwImages:boolean) {
  builder.addFieldInt8(17, +expandCwImages, +false);
}

static addShowReadPosts(builder:flatbuffers.Builder, showReadPosts:boolean) {
  builder.addFieldInt8(18, +showReadPosts, +true);
}

static addShowKarma(builder:flatbuffers.Builder, showKarma:boolean) {
  builder.addFieldInt8(19, +showKarma, +true);
}

static addJavascriptEnabled(builder:flatbuffers.Builder, javascriptEnabled:boolean) {
  builder.addFieldInt8(20, +javascriptEnabled, +true);
}

static addInfiniteScrollEnabled(builder:flatbuffers.Builder, infiniteScrollEnabled:boolean) {
  builder.addFieldInt8(21, +infiniteScrollEnabled, +true);
}

static addInterfaceLanguage(builder:flatbuffers.Builder, interfaceLanguageOffset:flatbuffers.Offset) {
  builder.addFieldOffset(22, interfaceLanguageOffset, 0);
}

static addTheme(builder:flatbuffers.Builder, themeOffset:flatbuffers.Offset) {
  builder.addFieldOffset(23, themeOffset, 0);
}

static addDefaultSortType(builder:flatbuffers.Builder, defaultSortType:SortType) {
  builder.addFieldInt8(24, defaultSortType, SortType.Active);
}

static addDefaultCommentSortType(builder:flatbuffers.Builder, defaultCommentSortType:CommentSortType) {
  builder.addFieldInt8(25, defaultCommentSortType, CommentSortType.Hot);
}

static endLocalUser(builder:flatbuffers.Builder):flatbuffers.Offset {
  const offset = builder.endObject();
  return offset;
}

}