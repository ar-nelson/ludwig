// automatically generated by the FlatBuffers compiler, do not modify

/* eslint-disable @typescript-eslint/no-unused-vars, @typescript-eslint/no-explicit-any, @typescript-eslint/no-non-null-assertion */

import * as flatbuffers from 'npm:flatbuffers';

export class BoardStats {
  bb: flatbuffers.ByteBuffer|null = null;
  bb_pos = 0;
  __init(i:number, bb:flatbuffers.ByteBuffer):BoardStats {
  this.bb_pos = i;
  this.bb = bb;
  return this;
}

static getRootAsBoardStats(bb:flatbuffers.ByteBuffer, obj?:BoardStats):BoardStats {
  return (obj || new BoardStats()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

static getSizePrefixedRootAsBoardStats(bb:flatbuffers.ByteBuffer, obj?:BoardStats):BoardStats {
  bb.setPosition(bb.position() + flatbuffers.SIZE_PREFIX_LENGTH);
  return (obj || new BoardStats()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

threadCount():bigint {
  const offset = this.bb!.__offset(this.bb_pos, 4);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : BigInt('0');
}

commentCount():bigint {
  const offset = this.bb!.__offset(this.bb_pos, 6);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : BigInt('0');
}

latestPostTime():bigint {
  const offset = this.bb!.__offset(this.bb_pos, 8);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : BigInt('0');
}

latestPostId():bigint {
  const offset = this.bb!.__offset(this.bb_pos, 10);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : BigInt('0');
}

subscriberCount():bigint {
  const offset = this.bb!.__offset(this.bb_pos, 12);
  return offset ? this.bb!.readUint64(this.bb_pos + offset) : BigInt('0');
}

usersActiveHalfYear():number {
  const offset = this.bb!.__offset(this.bb_pos, 14);
  return offset ? this.bb!.readUint32(this.bb_pos + offset) : 0;
}

usersActiveMonth():number {
  const offset = this.bb!.__offset(this.bb_pos, 16);
  return offset ? this.bb!.readUint32(this.bb_pos + offset) : 0;
}

usersActiveWeek():number {
  const offset = this.bb!.__offset(this.bb_pos, 18);
  return offset ? this.bb!.readUint32(this.bb_pos + offset) : 0;
}

usersActiveDay():number {
  const offset = this.bb!.__offset(this.bb_pos, 20);
  return offset ? this.bb!.readUint32(this.bb_pos + offset) : 0;
}

static startBoardStats(builder:flatbuffers.Builder) {
  builder.startObject(9);
}

static addThreadCount(builder:flatbuffers.Builder, threadCount:bigint) {
  builder.addFieldInt64(0, threadCount, BigInt('0'));
}

static addCommentCount(builder:flatbuffers.Builder, commentCount:bigint) {
  builder.addFieldInt64(1, commentCount, BigInt('0'));
}

static addLatestPostTime(builder:flatbuffers.Builder, latestPostTime:bigint) {
  builder.addFieldInt64(2, latestPostTime, BigInt('0'));
}

static addLatestPostId(builder:flatbuffers.Builder, latestPostId:bigint) {
  builder.addFieldInt64(3, latestPostId, BigInt('0'));
}

static addSubscriberCount(builder:flatbuffers.Builder, subscriberCount:bigint) {
  builder.addFieldInt64(4, subscriberCount, BigInt('0'));
}

static addUsersActiveHalfYear(builder:flatbuffers.Builder, usersActiveHalfYear:number) {
  builder.addFieldInt32(5, usersActiveHalfYear, 0);
}

static addUsersActiveMonth(builder:flatbuffers.Builder, usersActiveMonth:number) {
  builder.addFieldInt32(6, usersActiveMonth, 0);
}

static addUsersActiveWeek(builder:flatbuffers.Builder, usersActiveWeek:number) {
  builder.addFieldInt32(7, usersActiveWeek, 0);
}

static addUsersActiveDay(builder:flatbuffers.Builder, usersActiveDay:number) {
  builder.addFieldInt32(8, usersActiveDay, 0);
}

static endBoardStats(builder:flatbuffers.Builder):flatbuffers.Offset {
  const offset = builder.endObject();
  return offset;
}

static createBoardStats(builder:flatbuffers.Builder, threadCount:bigint, commentCount:bigint, latestPostTime:bigint, latestPostId:bigint, subscriberCount:bigint, usersActiveHalfYear:number, usersActiveMonth:number, usersActiveWeek:number, usersActiveDay:number):flatbuffers.Offset {
  BoardStats.startBoardStats(builder);
  BoardStats.addThreadCount(builder, threadCount);
  BoardStats.addCommentCount(builder, commentCount);
  BoardStats.addLatestPostTime(builder, latestPostTime);
  BoardStats.addLatestPostId(builder, latestPostId);
  BoardStats.addSubscriberCount(builder, subscriberCount);
  BoardStats.addUsersActiveHalfYear(builder, usersActiveHalfYear);
  BoardStats.addUsersActiveMonth(builder, usersActiveMonth);
  BoardStats.addUsersActiveWeek(builder, usersActiveWeek);
  BoardStats.addUsersActiveDay(builder, usersActiveDay);
  return BoardStats.endBoardStats(builder);
}
}