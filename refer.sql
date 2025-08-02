DROP DATABASE IF EXISTS douyin;
CREATE DATABASE douyin;
USE douyin;

-- ----------------------------
-- Table structure for user
-- ----------------------------
DROP TABLE IF EXISTS `user`;
CREATE TABLE `user` (
  `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT COMMENT '主键',
  `name` varchar(32) NOT NULL DEFAULT '' COMMENT '用户名称',
  `password` varchar(255) NOT NULL DEFAULT '' COMMENT '密码，已加密',
  `follow_count` bigint(20) unsigned NOT NULL DEFAULT '0' COMMENT '关注人数',
  `follower_count` bigint(20) unsigned NOT NULL DEFAULT '0' COMMENT '粉丝人数',
  `created_at` timestamp NULL DEFAULT NULL COMMENT '创建时间',
  `updated_at` timestamp NULL DEFAULT NULL ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
  `deleted_at` timestamp NULL DEFAULT NULL COMMENT '删除时间，软删除',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uni_name` (`name`) COMMENT '用户名称需要唯一'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='用户表';

-- ----------------------------
-- Table structure for relation
-- ----------------------------
DROP TABLE IF EXISTS `relation`;
CREATE TABLE `relation` (
  `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT COMMENT '主键',
  `user_id` bigint(20) unsigned NOT NULL DEFAULT '0' COMMENT '用户id',
  `to_user_id` bigint(20) unsigned NOT NULL DEFAULT '0' COMMENT '关注目标的用户id',
  `created_at` timestamp NULL DEFAULT NULL COMMENT '创建时间',
  `updated_at` timestamp NULL DEFAULT NULL ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
  `deleted_at` timestamp NULL DEFAULT NULL COMMENT '删除时间，软删除',
  PRIMARY KEY (`id`) USING BTREE,
  UNIQUE KEY `uk_relation` (`user_id`,`to_user_id`,`deleted_at`),
  KEY `user_relation` (`user_id`),
  KEY `user_relation_to` (`to_user_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='关注表';

-- ----------------------------
-- Table structure for video
-- ----------------------------
DROP TABLE IF EXISTS `video`;
CREATE TABLE `video` (
  `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT COMMENT '主键',
  `author_id` bigint(20) unsigned NOT NULL DEFAULT '0' COMMENT 'user表主键',
  `title` varchar(128) NOT NULL DEFAULT '' COMMENT '视频标题',
  `play_url` varchar(128) NOT NULL DEFAULT '' COMMENT '视频地址',
  `cover_url` varchar(128) NOT NULL DEFAULT '' COMMENT '封面地址',
  `favorite_count` int(15) unsigned NOT NULL DEFAULT '0' COMMENT '获赞数量',
  `comment_count` int(15) unsigned NOT NULL DEFAULT '0' COMMENT '评论数量',
  `created_at` timestamp NULL DEFAULT NULL COMMENT '创建时间',
  `updated_at` timestamp NULL DEFAULT NULL ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
  `deleted_at` timestamp NULL DEFAULT NULL COMMENT '删除时间，软删除',
  PRIMARY KEY (`id`),
  KEY `author_video` (`author_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='视频表';

-- ----------------------------
-- Table structure for message
-- ----------------------------
DROP TABLE IF EXISTS `message`;
CREATE TABLE `message` (
  `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT COMMENT '消息id',
  `to_user_id` bigint(20) unsigned NOT NULL DEFAULT '0' COMMENT '该消息接收者的id',
  `from_user_id` bigint(20) unsigned NOT NULL DEFAULT '0' COMMENT '该消息发送者的id',
  `contents` varchar(255) NOT NULL DEFAULT '' COMMENT '消息内容',
  `create_time` bigint(20) unsigned NOT NULL DEFAULT '0' COMMENT '自设创建时间(unix)',
  `created_at` timestamp NULL DEFAULT NULL COMMENT '创建时间',
  `updated_at` timestamp NULL DEFAULT NULL ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
  `deleted_at` timestamp NULL DEFAULT NULL COMMENT '删除时间，软删除',
  PRIMARY KEY (`id`),
  KEY `user_message_to` (`to_user_id`),
  KEY `user_message_from` (`from_user_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='消息表';
