/*************************************************************************
    > File Name: server.cpp
    > Author: Chen Tianzeng
    > Mail: 971859774@qq.com 
    > Created Time: 2019年11月04日 星期一 15时01分56秒
 ************************************************************************/

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <error.h>
#include <fcntl.h>
#include <mysql/mysql.h>
#include <algorithm>
#include <map>
#include <sys/epoll.h>
using namespace std;

class Server
{
	private:
		//socket
		int listenFd;
		int conFd;
		struct sockaddr_in servAddr,cliAddr,peerAddr;//add peerAddr
		int epollFd;
		struct epoll_event events[1024];

		//mysql
		MYSQL *con;
		MYSQL_RES *res;
		MYSQL_ROW row;

		//add peer ip
		char ipAddr[INET_ADDRSTRLEN];

		//add client and fd map
		map<int,string> fdToName;
	public:
		Server()
		{
			listenFd=0;
			conFd=0;
			epollFd=0;
			memset(&cliAddr,0,sizeof(cliAddr));
			memset(&servAddr,0,sizeof(servAddr));
			memset(&peerAddr,0,sizeof(peerAddr));//add,get client ip & port
			servAddr.sin_family=AF_INET;
			servAddr.sin_port=htons(9877);
			servAddr.sin_addr.s_addr=htonl(INADDR_ANY);
			
			//add peer ip
			memset(ipAddr,0,strlen(ipAddr));

			epollFd=epoll_create(1024);
			if(epollFd<0)
			{
				cerr<<"epoll_create error: "<<strerror(errno)<<endl;
				exit(0);
			}

			con=mysql_init(nullptr);
			if(!con)
			{
				cerr<<"sever error: "<<mysql_error(con)<<endl;
				exit(0);
			}
		}

		int Socket()
		{
			listenFd=socket(AF_INET,SOCK_STREAM,0);
			int flag=1;
			if(setsockopt(listenFd,SOL_SOCKET,SO_REUSEADDR,&flag,sizeof(flag))<0)
			{
				cerr<<"In socket,setsockopt SO_REUSADDR error: "<<strerror(errno)<<endl;
				return -1;
			}
			if(setsockopt(listenFd,IPPROTO_TCP,TCP_NODELAY,(void*)&flag,sizeof(flag))<0)
			{
				cerr<<"In socket,setsockopt TCP_NODELAY error: "<<strerror(errno)<<endl;
				return -1;
			}
			return listenFd;
		}

		int Bind()
		{
			return bind(listenFd,(struct sockaddr*)&servAddr,sizeof(servAddr));
		}

		int Listen()
		{
			return listen(listenFd,1024);
		}

		void Accept()
		{
			addFd(listenFd,true);//监听描述符.et模式下要用非阻塞,
			while(true)
			{
				//epoll会把即将发生的事件赋值到events数组中,不可以是空指针,要事先分配好
				int count=epoll_wait(epollFd,events,1024,-1);
				if(count<0)
				{
					cerr<<"epoll_wait error: "<<strerror(errno);
					return ;
				}

				for(int i=0;i<count;++i)
				{
					int _fd=events[i].data.fd;
					if(_fd==listenFd)
					{
						while(true)
						{
							socklen_t cliLen=sizeof(cliAddr);
							conFd=accept(listenFd,(struct sockaddr*)&cliAddr,&cliLen);//connect Fd,每次新的链接到来值会被重写
							if(conFd<0)
							{
								if(errno==EAGAIN)
									continue;
								else
								{
									cerr<<"accept error: "<<strerror(errno)<<endl;
									return ;
								}
							}
							else
							{
								addFd(conFd,false);
								char *ptr=nullptr;//duplicate login
								if(!login(ptr))
								{
									cerr<<"login error."<<endl;
								}
								break;
							}
						}//end while
					}
					else if(events[i].events&EPOLLIN)
					{
						//verification friend
					#if 0
						epoll_event event;
						event.data.fd=_fd;
						event.events=EPOLLIN;
						epoll_ctl(epollFd,EPOLL_CTL_MOD,_fd,&event);
					#endif
						char buff[30];
						memset(buff,0,30);
						int len=read(_fd,buff,30);
						if(len<=0)
						{
							if(len==0)
							{
								cout<<"client "<<fdToName[_fd]<<" exit server"<<endl;
								close(_fd);
							}
							else
							{
								cerr<<"In friendVerification,read client friendName error: "<<strerror(errno)<<endl;
							}
						}
						else
						{
							buff[len]='\0';
						
							if(buff[len-1]!='@')
							{
								login(buff);
							}
							else
							{
								string friendName(buff,len-1);
								cout<<"wrapName: "<<friendName<<endl;
								string ipAndPort;
								bool resVerFriend=friendVerification(_fd,friendName,ipAndPort);
								if(resVerFriend==false)
								{
									int writeLen=write(_fd,&resVerFriend,sizeof(bool));
									if(writeLen<=0)
									{
										cerr<<"return to client friendVerification result error: "<<strerror(errno)<<endl;
										return ;
									}
									cerr<<"friendVerification failed."<<endl;
								}
								else
								{
									cout<<"friendVerification success"<<endl;
									int writeLen=write(_fd,&resVerFriend,sizeof(bool));
									if(writeLen<=0)
									{
										cerr<<"return to client friendVerification result error: "<<strerror(errno)<<endl;
										return ;
									}

									writeLen=write(_fd,ipAndPort.c_str(),ipAndPort.size()-1);
									if(writeLen<=0)
									{
										cerr<<"return to client,write error: "<<strerror(errno);
										return ;
									}
									else if(writeLen<ipAndPort.size()-1)
									{
										cerr<<"return to client ipAndPort error"<<endl;
										return ;
									}//if
								}//else
							}//else
						}//else
					}
				}//end for
			}//end while
			return ;
		}//end Accept

		bool connectDb(const string& host,const string& user,const string& pwd,const string& dbName)
		{
			if(host.empty()||user.empty()||pwd.empty()||dbName.empty())
			{
				cerr<<"In connectDb,host or user or pwd or dbName is empty."<<endl;
				return false;
			}

			con=mysql_real_connect(con,host.c_str(),user.c_str(),pwd.c_str(),dbName.c_str(),0,nullptr,0);
			if(!con)
			{
				cerr<<"connectDb error: "<<mysql_error(con)<<endl;
				exit(0);
			}
			return true;
		}

		~Server()
		{
			if(con)
				mysql_close(con);
			
			close(listenFd);
			close(conFd);
			close(epollFd);
		}
	private:
		void addFd(int sockfd,bool enableEt)
		{
			if(sockfd<=0)
			{
				cerr<<"In addFd, sockfd is less zero."<<endl;
				return ;
			}

			epoll_event event;
			event.data.fd=sockfd;
			event.events=EPOLLIN;
			if(enableEt)
				event.events|=EPOLLET;
			if(epoll_ctl(epollFd,EPOLL_CTL_ADD,sockfd,&event)<0)
			{
				cerr<<"In addFd,epoll_ctl error: "<<strerror(errno)<<endl;
				return ;
			}
			if(enableEt)
				setnonblocking(sockfd);

			return ;
		}

		int setnonblocking(int sockfd)
		{
			if(sockfd<=0)
			{
				cerr<<"setnonblocking error,sockfd<=0"<<endl;
				exit(1);
			}

			int oldOption=fcntl(sockfd,F_GETFL);
			int newOption=oldOption|O_NONBLOCK;
			fcntl(sockfd,F_SETFL,newOption);
			return oldOption;
		}

		bool login(char *ptr)
		{
			//verificaion userName && pwd legality
			char message[30];
			memset(message,0,strlen(message));
			int mesLen=0;
			if(!ptr)
			{
			AGAIN:
				mesLen=read(conFd,message,30);
				if(mesLen<0&&errno==EAGAIN)
					goto AGAIN;
				else if(mesLen<0)
				{
					cerr<<"In Accept,read message error: "<<strerror(errno)<<endl;
					return false;
				}
				//cout<<message<<endl;
			}
			else
			{
				memcpy(message,ptr,strlen(ptr));
				mesLen=strlen(ptr);
			}

			char name[10];
			memset(name,0,strlen(name));
			int index=0;
			for(int i=0;i<mesLen;++i)
			{
				if(message[i]!=',')
					name[index++]=message[i];
				else
					break;
			}
			name[index]='\0';
			fdToName[conFd]=name;
			char pwd[10];
			memset(pwd,0,strlen(pwd));
			int j=0;
			for(int i=index+1;i<mesLen;++i)
			{
				pwd[j++]=message[i];
			}
			pwd[j]='\0';
			
			string sql("select * from user where name='");
			sql+=name;
			sql+="' and pwd='";
			sql+=pwd;
			sql+="';";
			bool execRes=execQuery(sql);
			//把查数据库结果返回给客户端
			ssize_t writeLen=write(conFd,(void*)&execRes,sizeof(bool));
			if(writeLen<0)
			{
				cerr<<"return login result error: "<<strerror(errno)<<endl;
				return false;
			}
			if(execRes)
				cout<<name<<",welcome to server! processing connection conFd is: "<<conFd<<endl;
			else
			{
				cout<<name<<",login server falied! processing connect conFd is: "<<conFd<<endl;
				return false;
			}
			
			//get perr ip & port
			socklen_t peerLen=sizeof(peerAddr);
			if(getpeername(conFd,(struct sockaddr*)&peerAddr,&peerLen)==-1)
			{
				cerr<<"get client ip and port error."<<endl;
				exit(-1);
			}
			if(inet_ntop(AF_INET,&peerAddr.sin_addr,ipAddr,sizeof(ipAddr))==nullptr)
			{
				cerr<<"conversion ip error."<<endl;
				exit(1);
			}
			uint16_t port=ntohs(peerAddr.sin_port);
			if(port<=0)
			{
				cerr<<"conversion port error."<<endl;
				exit(1);
			}
			string ip(ipAddr);
			cout<<"ip="<<ip<<",port="<<port<<endl;
			//write ip and port to user table
			if(insertIp(name,ip)==false)
			{
				cerr<<"update ip to table user error."<<endl;
				exit(1);
			}
			if(insertPort(name,port)==false)
			{
				cerr<<"update port to table user error."<<endl;
				exit(1);
			}
			return true;
		}
		
		bool friendVerification(int _fd,const string& friendName,string& ipAndPort)
		{

			string sql("select * from friend where selfName='");
			sql+=fdToName[_fd];
			sql+="' and friendName='";
			sql+=friendName;
			sql+="';";
			//cout<<sql<<endl;
			
			bool execRes=execQuery(sql);
			if(execRes==false)
			{
				cerr<<"In friendVerification,execQuery return false"<<endl;
				return false;
			}
			return execQueryIpAndPort(ipAndPort,friendName);		
		}

		bool  execQueryIpAndPort(string& ipAndPort,const string& friendName)
		{
			string sql("select ip,port from user where name='");
			sql+=friendName;
			sql+="';";

			if(mysql_query(con,sql.c_str()))
			{
				cerr<<"query error: "<<mysql_error(con)<<endl;
				exit(1);
			}
			else
			{
				//get query result
				res=mysql_store_result(con);
				if(res)
				{
					for(int i=0;i<mysql_num_rows(res);++i)
					{
						row=mysql_fetch_row(res);
						if(row<0)
							break;
						
						for(int j=0;j<mysql_num_fields(res);++j)
						{
							string tmp(row[j]);
							ipAndPort+=tmp;
							ipAndPort+=",";
						}
					}
				}
				else
				{
					if(mysql_field_count(con)==0)
						int numRows=mysql_affected_rows(con);
					else
					{
						cerr<<"get result error: "<<mysql_error(con)<<endl;
						return false;
					}
				}
			}
			mysql_free_result(res);
			return true;
		}

		bool execQuery(string sql)
		{
			if(sql.empty())
			{
				cerr<<"sql is empty"<<endl;
				return false;
			}

			//mysql_query success return 0,failed return Nozero
			bool isZero=false;
			if(mysql_query(con,sql.c_str()))
			{
				cerr<<"mysql_query: "<<mysql_error(con)<<endl;
				exit(0);
			}
			else
			{
				res=mysql_store_result(con);
				uint64_t resNums=mysql_num_rows(res);
				isZero=!resNums?false:true;
				do
				{
					#if 0
						除了select会返回的一个结果集外,还会返回另外一个结果集用
						于标示存储过程执行的状态,所以要用mysql_free_result()清除
					#endif
					if(mysql_field_count(con)>0)
					{
						res=mysql_store_result(con);
						mysql_free_result(res);
					}
				}while(!mysql_next_result(con));
			}
			return isZero?true:false;
		}
		
		bool insertIp(string name,string str)
		{
			if(name.empty()||str.empty())
				return false;

			string updateIp("update user set ip='");
			updateIp+=str;
			updateIp+="' where name='";
			updateIp+=name;
			updateIp+="'";
			cout<<updateIp<<endl;
			if(mysql_query(con,updateIp.c_str()))
			{
				cerr<<"mysql_query: "<<mysql_error(con)<<endl;
				exit(1);
			}
			return true;
		}

		bool insertPort(string name,int port)
		{
			if(name.empty()||port<=0)
				return false;

			string updatePort("update user set port=");
			updatePort+=to_string(port);
			updatePort+=" where name='";
			updatePort+=name;
			updatePort+="'";
			cout<<updatePort<<endl;
			if(mysql_query(con,updatePort.c_str()))
			{
				cerr<<"mysql_query: "<<mysql_error(con)<<endl;
				exit(1);
			}
			return true;
		}
};

int main()
{
	Server server;
	bool flag=server.connectDb("localhost","root","root","chat");//databaseName,connect database
	if(!flag)
		return 0;

	int res=server.Socket();
	if(res<0)
	{
		cerr<<"socket error: "<<strerror(errno)<<endl;
		return 0;
	}

	res=server.Bind();
	if(res<0)
	{
		cerr<<"bind error: "<<strerror(errno)<<endl;
		return 0;
	}

	res=server.Listen();
	if(res<0)
	{
		cerr<<"listen error: "<<strerror(errno)<<endl;
		return 0;
	}

	server.Accept();
	return 0;
}

//-L /usr/lib64 -lmysqlclient
