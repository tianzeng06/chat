CREATE DATABASE chat;

CREATE TABLE user(
	name varchar(30),
	pwd varchar(30));
insert into user values('ctz','123'),('abc','456');

CREATE TABLE friend(
	selfName varchar(30),
	friendName varchar(30));
insert into friend values('ctz','abc'),('abc','ctz');
