#数据库表
#创建数据库
DROP DATABASE IF EXISTS `cloud_disk_db`;
CREATE DATABASE `cloud_disk_db`;

#使用数据库
use `cloud_disk_db`;


DROP TABLE IF EXISTS `file_info`;
CREATE TABLE `file_info` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT COMMENT '文件序号，自动递增，主键',
  `md5` varchar(256) NOT NULL COMMENT '文件md5',
  `file_id` varchar(256) NOT NULL COMMENT '文件id:/group1/M00/00/00/xxx.png',
  `url` varchar(512) NOT NULL COMMENT '文件url 192.168.52.139:80/group1/M00/00/00/xxx.png',
  `size` bigint(20) DEFAULT '0' COMMENT '文件大小, 以字节为单位',
  `type` varchar(32) DEFAULT '' COMMENT '文件类型： png, zip, mp4……',
  `count` int(11) DEFAULT '0' COMMENT '文件引用计数,默认为1。每增加一个用户拥有此文件，此计数器+1',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_md5` (`md5`)  
) ENGINE=InnoDB AUTO_INCREMENT=70 DEFAULT CHARSET=utf8 COMMENT='文件信息表';


DROP TABLE IF EXISTS `share_file_list`;
CREATE TABLE `share_file_list` (
  `id` int(11) NOT NULL AUTO_INCREMENT COMMENT '编号',
  `user` varchar(32) NOT NULL COMMENT '文件所属用户',
  `md5` varchar(256) NOT NULL COMMENT '文件md5',
  `file_name` varchar(128) DEFAULT NULL COMMENT '文件名字',
  `pv` int(11) DEFAULT '1' COMMENT '文件下载量，默认值为1，下载一次加1',
  `create_time` timestamp NULL DEFAULT CURRENT_TIMESTAMP COMMENT '文件共享时间',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB AUTO_INCREMENT=16 DEFAULT CHARSET=utf8 COMMENT='共享文件列表';


DROP TABLE IF EXISTS `share_picture_list`;
CREATE TABLE `share_picture_list` (
  `id` int(11) NOT NULL AUTO_INCREMENT COMMENT '编号',
  `user` varchar(32) NOT NULL COMMENT '文件所属用户',
  `filemd5` varchar(256) NOT NULL COMMENT '文件md5',
  `file_name` varchar(128) DEFAULT NULL COMMENT '文件名字',
  `urlmd5` varchar(256) NOT NULL COMMENT '图床urlmd5',
  `key` varchar(8) NOT NULL COMMENT '提取码',
  `pv` int(11) DEFAULT '1' COMMENT '文件下载量，默认值为1，下载一次加1',
  `create_time` timestamp NULL DEFAULT CURRENT_TIMESTAMP COMMENT '文件创建时间',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB AUTO_INCREMENT=16 DEFAULT CHARSET=utf8 COMMENT='图床文件列表';


DROP TABLE IF EXISTS `user_file_count`;
CREATE TABLE `user_file_count` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `user` varchar(128) NOT NULL COMMENT '文件所属用户',
  `count` int(11) DEFAULT NULL COMMENT '拥有文件的数量',
  PRIMARY KEY (`id`),
  UNIQUE KEY `user_UNIQUE` (`user`)
) ENGINE=InnoDB AUTO_INCREMENT=5 DEFAULT CHARSET=utf8 COMMENT='用户文件数量表';


DROP TABLE IF EXISTS `user_file_list`;
CREATE TABLE `user_file_list` (
  `id` int(11) NOT NULL AUTO_INCREMENT COMMENT '编号',
  `user` varchar(32) NOT NULL COMMENT '文件所属用户',
  `md5` varchar(256) NOT NULL COMMENT '文件md5',
  `create_time` timestamp NULL DEFAULT CURRENT_TIMESTAMP COMMENT '文件创建时间',
  `file_name` varchar(128) DEFAULT NULL COMMENT '文件名字',
  `shared_status` int(11) DEFAULT NULL COMMENT '共享状态, 0为没有共享， 1为共享',
  `pv` int(11) DEFAULT NULL COMMENT '文件下载量，默认值为0，下载一次加1',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB AUTO_INCREMENT=30 DEFAULT CHARSET=utf8 COMMENT='用户文件列表';


DROP TABLE IF EXISTS `user_info`;
CREATE TABLE `user_info` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT COMMENT '用户序号，自动递增，主键',
  `user_name` varchar(32) NOT NULL DEFAULT '' COMMENT '用户名称',
  `nick_name` varchar(32) CHARACTER SET utf8mb4 NOT NULL DEFAULT '' COMMENT '用户昵称',
  `password` varchar(32) NOT NULL DEFAULT '' COMMENT '密码',
  `phone` varchar(16) NOT NULL DEFAULT '' COMMENT '手机号码',
  `email` varchar(64) DEFAULT '' COMMENT '邮箱',
  `create_time` timestamp NULL DEFAULT CURRENT_TIMESTAMP COMMENT '时间',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_nick_name` (`nick_name`),
  UNIQUE KEY `uq_user_name` (`user_name`)
) ENGINE=InnoDB AUTO_INCREMENT=14 DEFAULT CHARSET=utf8 COMMENT='用户信息表';

DROP TABLE IF EXISTS `file_ai_desc`;
CREATE TABLE `file_ai_desc` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT COMMENT '编号',
  `md5` varchar(256) NOT NULL COMMENT '文件MD5（唯一索引）',
  `description` text COMMENT 'AI生成的文件描述',
  `embedding` mediumblob COMMENT '1024维float向量（4096字节）',
  `faiss_id` int(11) DEFAULT NULL COMMENT 'FAISS索引中的ID',
  `model` varchar(64) DEFAULT '' COMMENT '使用的AI模型名',
  `status` tinyint(4) DEFAULT '0' COMMENT '状态：0=待处理 1=完成 2=失败',
  `create_time` timestamp NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_md5` (`md5`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='AI文件描述与向量表';

DROP TABLE IF EXISTS `share_picture_list`;
CREATE TABLE `share_picture_list` (
  `id` int(11) NOT NULL AUTO_INCREMENT COMMENT '编号',
  `user` varchar(32) NOT NULL COMMENT '文件所属用户',
  `filemd5` varchar(256) NOT NULL COMMENT '文件md5',
  `file_name` varchar(128) DEFAULT NULL COMMENT '文件名字',
  `urlmd5` varchar(256) NOT NULL COMMENT '图床urlmd5',
  `key` varchar(8) NOT NULL COMMENT '提取码',
  `pv` int(11) DEFAULT '1' COMMENT '文件下载量，默认值为1，下载一次加1',
  `create_time` timestamp NULL DEFAULT CURRENT_TIMESTAMP COMMENT '文件创建时间',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='图床文件列表';

CREATE TABLE IF NOT EXISTS share_picture_list (
    id INT AUTO_INCREMENT PRIMARY KEY,
    user VARCHAR(128) NOT NULL,
    filemd5 VARCHAR(128) NOT NULL,
    file_name VARCHAR(256) NOT NULL,
    urlmd5 VARCHAR(128) NOT NULL UNIQUE,
    `key` VARCHAR(16) NOT NULL,
    pv INT DEFAULT 0,
    create_time DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS file_ai_desc (
    id INT AUTO_INCREMENT PRIMARY KEY,
    md5 VARCHAR(128) NOT NULL UNIQUE,
    description TEXT COMMENT 'AI生成的摘要内容',
    faiss_id INT DEFAULT -1 COMMENT 'Faiss向量库索引ID',
    status INT DEFAULT 0 COMMENT '处理状态(1为已处理)',
    file_name VARCHAR(256) DEFAULT ''
);
