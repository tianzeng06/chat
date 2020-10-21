/*************************************************************************
    > File Name: client.cpp
    > Author: Chen Tianzeng
    > Mail: 971859774@qq.com 
    > Created Time: 2019年11月04日 星期一 09时37分29秒
 ************************************************************************/

#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>//TCP_NODELAY
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
using namespace std;

class Client
{
	private:
		//socket fd
		int sockfd,udpFd;

		struct sockaddr_in servAddr,localAddr,friendAddr;

		//create new udp socket
		string friendIP;
		int friendPort,localPort;
	public:
		Client()//初始化sockfd和sockaddr_in
		{
			sockfd=socket(AF_INET,SOCK_STREAM,0);
			int opt=1;
			int res=setsockopt(sockfd,IPPROTO_TCP,TCP_NODELAY,(void*)&opt,sizeof(opt));
			if(res==-1)
				cerr<<"setsockopt error:"<<strerror(errno)<<endl;

			memset(&servAddr,0,sizeof(servAddr));
			servAddr.sin_family=AF_INET;
			servAddr.sin_port=htons(9877);

			memset(&localAddr,0,sizeof(localAddr));
			memset(&friendAddr,0,sizeof(friendAddr));
		}

		//根据主机名字得到ip
		int getIpAddrByHostname(char* hostname)
		{
			if(!hostname)
			{
				cerr<<"hostname is nullptr"<<endl;
				return 1;
			}
			
			struct addrinfo *res=nullptr,hints;
			memset(&hints,0,sizeof(hints));
			hints.ai_family=AF_INET;
			hints.ai_socktype=SOCK_STREAM;
		    int ret=getaddrinfo(hostname,nullptr,&hints,&res);
			if(!ret)
			{
				//in_addr is:4 bytes with inet_pton
				struct in_addr addr=((struct sockaddr_in*)res->ai_addr)->sin_addr;
				memcpy(&servAddr.sin_addr,&addr.s_addr,16);
			#if 0
				//测试转换是否正确,getaddrinfo得到的已经为网络字节序无需转
				char ip[16];
				inet_ntop(AF_INET,&servAddr.sin_addr,ip,INET_ADDRSTRLEN);
				cout<<ip<<endl;
			#endif
				freeaddrinfo(res);
			}
			return ret;
		}

		//向服务器发起连接
		int Connect()
		{
			return connect(sockfd,(struct sockaddr*)&servAddr,sizeof(servAddr));
		}
		
		//登录到服务器
		bool login(char* name,char* pwd)
		{
			if(!name||!pwd)
			{
				cerr<<"please input name or pwd"<<endl;
				return false;
			}
			
			int strLen=strlen(name)+strlen(pwd);
			char str[strLen+1];
			memset(str,0,strLen);
			memcpy(str,name,strlen(name));
			memcpy(str+strlen(name),",",1);
			memcpy(str+strlen(name)+1,pwd,strlen(pwd));

			//开始向服务器验证
			ssize_t res=write(sockfd,str,strLen+1);
			if(res==-1)
			{
				cerr<<"login error."<<endl;
				return false;
			}
			
			bool flag=false;
			res=read(sockfd,&flag,1);
			if(res==-1)
			{
				cerr<<"receive server login information error."<<endl;
				return false;
			}
			return flag;
		}
		
		bool verificationName(const char* name)
		{
			if(!name)
			{
				cerr<<"frien name is empty."<<endl;
				return false;
			}
			
			int lens=strlen(name)+1;
			char wrapName[lens];
			memset(wrapName,0,lens);
			memcpy(wrapName,name,lens-1);
			wrapName[lens-1]='@';
			ssize_t len=write(sockfd,wrapName,lens);
			if(len==-1)
			{
				cerr<<"verificationName write error."<<endl;
				return false;
			}
			bool verRes=false;
			len=read(sockfd,&verRes,1);
			//cout<<"verRes="<<verRes<<endl;
			if(len<=0)
			{
				cerr<<"read server return verificationName result error: "<<strerror(errno);
				exit(1);
			}
			if(verRes==false)
				return verRes;

			//fflush(stdin);
			//fflush(stdout);
			char str[30];
			memset(str,0,30);
			len=read(sockfd,&str,30);
			str[len]='\0';
			if(len<=0)
			{
				cerr<<"receive server ipAndPort error."<<endl;
				return false;
			}
			string ipAndPort(str);
			int pos=ipAndPort.find(',');
			if(pos==ipAndPort.npos)
			{
				cerr<<"receive server ipAndPort form error."<<endl;
				return false;
			}
			//init friend ip and port
			friendIP=ipAndPort.substr(0,pos);
			friendPort=stoi(ipAndPort.substr(pos+1,ipAndPort.size()-pos));
			//cout<<"in verificationName:friendIP:="<<friendIP<<",friendPort="<<friendPort<<endl;
			return true;
		}
		//初始化本地服务
		bool initLocalServer()
		{
			socklen_t len=sizeof(localAddr);
			if(getsockname(sockfd,(struct sockaddr*)&localAddr,&len)!=0)//获取与服务器连接时创建的端口号
			{
				cerr<<"get local port error:"<<strerror(errno);
				return false;
			}
			localPort=localAddr.sin_port;
			//cout<<"in initLocalServer: localPort="<<ntohs(localPort)<<endl;
			udpFd=socket(AF_INET,SOCK_DGRAM,0);
			localAddr.sin_port=localPort;
			localAddr.sin_family=AF_INET;
			localAddr.sin_addr.s_addr=htonl(INADDR_ANY);
			if(bind(udpFd,(struct sockaddr*)&localAddr,sizeof(localAddr))!=0)
			{
				cerr<<"initLocalServe error: "<<strerror(errno)<<endl;
				return false;
			}
			if(initFriendAddr()==false)
			{
				cerr<<"initFriendAddr error"<<endl;
				return false;
			}
			return true;
		}
		bool initFriendAddr()
		{
			friendAddr.sin_family=AF_INET;
			friendAddr.sin_port=htons(friendPort);
			inet_pton(AF_INET,friendIP.c_str(),&friendAddr.sin_addr);
			return true;
		}
		//收发消息
		void sendAndRecvMessage(FILE* fp)
		{
			if(!fp)
			{
				cerr<<"fp is nullptr"<<endl;
				return ;
			}
			
			fd_set rest;
			FD_ZERO(&rest);
			while(true)
			{
				if(FD_ISSET(udpFd,&rest)==0)
					FD_SET(udpFd,&rest);
				if(FD_ISSET(fileno(fp),&rest)==0)
					FD_SET(fileno(fp),&rest);

				int maxFd=max(fileno(fp),udpFd)+1;
				select(maxFd,&rest,nullptr,nullptr,nullptr);

				char buff[1024];
				memset(buff,0,1024);
				socklen_t len=sizeof(friendAddr);
				if(FD_ISSET(udpFd,&rest))//套接字可读
				{
					int n=recvfrom(udpFd,buff,1024,0,(struct sockaddr*)&friendAddr,&len);
					if(n<=0)
					{
						cerr<<"read messg from friend error."<<endl;
					}
					string messHead("receive message is: ");
					write(fileno(stdout),messHead.c_str(),messHead.size());
					write(fileno(stdout),buff,len);
					memset(buff,0,1024);
				}
				else if(FD_ISSET(fileno(fp),&rest))//标准输入可读
				{
					int n=read(fileno(fp),buff,1024);
					sendto(udpFd,buff,n,0,(struct sockaddr*)&friendAddr,len);
					memset(buff,0,1024);
					FD_CLR(fileno(fp),&rest);
				}
				else
				{
					cerr<<"select error"<<endl;
					return ;
				}
			}
			return ;
		}
		~Client()
		{
			close(sockfd);
		}
};

int main(int argc,char **argv)
{
	if(argc<2)
	{
		cerr<<"please input server name"<<endl;
		return 0;
	}
	
	Client client;
	int res=client.getIpAddrByHostname(argv[1]);
	if(res)
	{
		cerr<<"getIpAddrByHostname error: "<<strerror(errno)<<endl;
		return 0;
	}
	
	res=client.Connect();
	if(res)
	{
		cerr<<"Connect error: "<<strerror(errno)<<endl;
		return 0;
	}

	cout<<"please input userName and password"<<endl;
	char name[10],pwd[10];
	int count=0;
AGAIN:
	++count;
	cin>>name>>pwd;
	bool status=client.login(name,pwd);
	if(status)
		cout<<"login success"<<endl;
	else
	{
		if(count==3)
			return 0;
		cerr<<"login failure,userName or pwd error.please Re input,"<<endl;
		goto AGAIN;
	}

	cout<<"please input friend name."<<endl;
	for(count=0;count<3;++count)
	{
		char friendName[10];
		cin>>friendName;
		status=client.verificationName(friendName);
		if(status)
		{
			cout<<"friend name verification success."<<endl;
			break;
		}
		else
		{
			cerr<<"friend name verification error."<<endl;
		}
	}
	if(count==4)
		return 0;

	status=client.initLocalServer();
	if(status==false)
		return 1;

	client.sendAndRecvMessage(stdin);
	
	return 0;
}
//-L /usr/lib64 -lmysqlclient

