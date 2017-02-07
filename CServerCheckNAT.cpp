//
// Created by sxh on 2017/2/6.
//


#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string.h>
#include <sys/time.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>

enum enumServerCmd
{
    SCmd_ClientCmd=1  //客户端连接主服务器
    ,SCmd_ClientCmdSub  //客户端向从服务器发包
    ,SCmd_ClientCmdToSub  //客户端需要向从服务器发送数据包
    ,SCmd_ClientCmdFinish //检测完成
    ,SCmd_SubNeedToClient //主服务器通知从服务器连接client. 带上client的ip和端口.
    ,SCmd_SubNeedToClientResponse //发包结果反馈给主服务器
    ,SCmd_SubToClinet    //从服务器发包给客户端
    ,SCmd_SubToClinetResponse  //客户端响应从服务器
};

enum enumTypeNAT
{
    NAT_Unknown = 0
    ,NAT_FullCone
    ,NAT_RestrictedCone
    ,NAT_PortRestrictedCone
    ,NAT_Symmetric
};


#ifdef __ANDROID__
#include <android/log.h>
#define myprintf(...) __android_log_print(ANDROID_LOG_INFO,"FPlus",__VA_ARGS__)
#else
#define myprintf(...) printf(__VA_ARGS__)
#endif





struct tagClientInfo
{
    timeval mStartTime;
    union
    {
        long long val;
        struct
        {
            long ip;
            long port;
        };
    } ipPort;
};



struct tagThreadParam
{
    int ip;
    int port;
    void *pVoid;
};



class CServerCheckNATMain
{
    int mSock;
    //配置的服务器IP和端口
    char mSubServerIP[32];
    u_short mSubServerPort;
    u_short mLocalPort;
    std::map<long long,tagClientInfo> mClients;

    //收到SCmd_SubNeedToClient请求时的IP和端口
    int mNotifyServerIP;
    u_short mNotifyServerPort;

public:
    CServerCheckNATMain()
    {
        mSock = socket(AF_INET,SOCK_DGRAM,0);
        strcpy(mSubServerIP,"192.168.10.11");
        mSubServerPort = 18902;
        mLocalPort = 18902;

        mNotifyServerIP = inet_addr(mSubServerIP);
        mNotifyServerPort  = htons(mSubServerPort);

        loadConfig();

        myprintf("localPort:%d\nSubServer:%s:%d\n",mLocalPort,mSubServerIP,mSubServerPort);
    }
    ~CServerCheckNATMain()
    {
        close(mSock);
    }

    void loadConfig()
    {
        FILE *pFile = fopen("config.cfg","rb");
        if(pFile)
        {
            char buf[0x100]={0};
            fread(buf,1,0x100,pFile);
            fclose(pFile);
            char delim[]=", \t\r\n";
            char ip[0x20]={0};
            char localPort[0x10]={0};
            char subPort[0x10]={0};
            char* res[3] = {localPort,ip,subPort};
            int i = 0;
            char *pCh = strtok(buf,delim);
            while(pCh&&i<3)
            {
                strcpy(res[i],pCh);
                pCh = strtok(NULL,delim);
                ++i;
            }
            strcpy(mSubServerIP, ip);
            mSubServerPort = (u_short)atoi(subPort);
            mLocalPort = (u_short)atoi(localPort);

        }
    }

    void addClient(long ip, long port)
    {
        tagClientInfo client;
        client.ipPort.ip = ip;
        client.ipPort.port = port;
        gettimeofday(&client.mStartTime,NULL);
        mClients.insert(std::pair<long long,tagClientInfo>(client.ipPort.val,client));
    }

    void startServer()
    {
        myprintf("begin server\n");
        ssize_t ret = 0;
        sockaddr_in srvAddr = {0};
        srvAddr.sin_addr.s_addr = INADDR_ANY;
        srvAddr.sin_family = AF_INET;
        srvAddr.sin_port = htons(mLocalPort);

        ret = bind(mSock, (sockaddr*)&srvAddr, sizeof(sockaddr_in));
        if(ret != 0)
        {
            myprintf("bind failed ret=%d err:%s\n", errno, strerror(errno));
            return;
        }

        char buf[0x100]= {0};
        sockaddr_in rcvAddr={0};



        while(1)
        {
            socklen_t sockLen = sizeof(sockaddr_in);
            myprintf("wait recv data......\n");
            ret = recvfrom(mSock, buf, 0x100,0,(sockaddr*)&rcvAddr, &sockLen);
            if(ret <= 0)
            {
                myprintf("recv failed ret=%d err:%s\n", errno, strerror(errno));
                break;
            }
            char flag = buf[0];
            switch(flag)
            {
                case SCmd_ClientCmd: //客户端请求判断自己的NAT类型
                    myprintf("client com\n");
                    //addClient(rcvAddr.sin_addr.s_addr, rcvAddr.sin_port); //添加到客户端列表
                    NotifySubNeedToClient(rcvAddr.sin_addr.s_addr, rcvAddr.sin_port); //通知从服务器去连接客户端
                    break;
                case SCmd_SubNeedToClient: //主服务器通知连接客户端. 则用新socket去连接客户端
                    myprintf("Sub(%s:%d) need to client\n",inet_ntoa(rcvAddr.sin_addr), ntohs(rcvAddr.sin_port));
                    mNotifyServerIP = rcvAddr.sin_addr.s_addr;
                    mNotifyServerPort = rcvAddr.sin_port;
                    onSubNeedToClient(buf,ret);
                    break;
                case SCmd_SubNeedToClientResponse: //从服务器响应主服务器的 连接客户端结果反馈.
                    myprintf("response sub need to client\n");
                    onSubNeedToClientResponse(buf,ret);
                    break;
                case SCmd_ClientCmdSub: //客户端连接从服务器的命令. 在这里判断两次的IP和端口是否相同
                    myprintf("client sub com %s:%d\n", inet_ntoa(rcvAddr.sin_addr), ntohs(rcvAddr.sin_port));
                    onClientCmdSub(rcvAddr.sin_addr.s_addr, rcvAddr.sin_port,buf,ret);
                    break;

                default:
                    myprintf("unknown cmd type: %d\n", flag);
                    break;
            }
        }

        myprintf("end server\n");

    }



    static void *ClientCmdSubThread(void *pVoid)
    {
        myprintf("ClientCmdSubThread subToClient begin\n");
        tagThreadParam *p = (tagThreadParam*)pVoid;
        int ip = p->ip;
        int port= p->port;
        CServerCheckNATMain *pThis = (CServerCheckNATMain*)p->pVoid;
        bool bSuccess = pThis->subToClient(ip,port);
        pThis->responseClient(ip,port,SCmd_ClientCmdFinish,bSuccess?NAT_RestrictedCone:NAT_PortRestrictedCone);
        delete p;
        myprintf("ClientCmdSubThread subToClient end\n");
        return NULL;
    }

    void onClientCmdSub(int ip, int port,char *data, int len)
    {
        //ip和端口是否相同,
        int mip = *(int*)&data[1];
        u_short mport = *(u_short*)&data[5];
        if(ip != mip || port != mport) //不同则是Symmetric NAT
        {
            responseClient(ip,port,SCmd_ClientCmdFinish,NAT_Symmetric);
        }
        else //相同,则再次连接客户端
        {
            tagThreadParam *param = new tagThreadParam;
            param->ip = ip;
            param->port = port;
            param->pVoid = this;
            pthread_t thread=0;
            pthread_create(&thread,NULL, ClientCmdSubThread, param);
            //bool bSuccess = subToClient(ip,port); //成功,则是RestrictedCone, 否则是PortRestrictedCone
            //responseClient(ip,port,SCmd_ClientCmdFinish,bSuccess?NAT_RestrictedCone:NAT_PortRestrictedCone);
        }

    }

    static void* SubNeedToClientThread(void *param)
    {
        myprintf("SubNeedToClientThread subToClient begin\n");
        tagThreadParam *p = (tagThreadParam*)param;
        int ip = p->ip;
        int port= p->port;
        CServerCheckNATMain *pThis = (CServerCheckNATMain*)p->pVoid;
        bool bSuccess = pThis->subToClient(ip,port);
        pThis->ResponseSubNeedToClient(ip,port,bSuccess);
        delete p;
        myprintf("SubNeedToClientThread subToClient end\n");
        return NULL;
    }


    //从服务器收到 主服务器发来的连接客户端的请求
    void onSubNeedToClient(char *data, int len)
    {
        int ip = *(int*)&data[1];
        u_short port = *(u_short*)&data[5];
        tagThreadParam *param = new tagThreadParam;
        param->ip = ip;
        param->port = port;
        param->pVoid = this;
        pthread_t thread=0;
        pthread_create(&thread,NULL, SubNeedToClientThread, param);

    }

    void onSubNeedToClientResponse( char* data, int len)
    {
        responseClient(*(int*)&data[1],*(u_short*)&data[5], data[7]>0?SCmd_ClientCmdFinish:SCmd_ClientCmdToSub,NAT_FullCone);
    }


    void responseClient(int ip, int port, enumServerCmd cmd, char type)
    {
        char buf[0x20]= {0};
        buf[0] = cmd;
        buf[1] = type;
        *(int*)&buf[2] = ip;
        *(u_short*)&buf[6] = port;
        *(int*)&buf[8] = inet_addr(mSubServerIP);
        *(u_short*)&buf[12] = htons(mSubServerPort);
        mySend(mSock, ip,port,buf,0x10);
        if(cmd == SCmd_ClientCmdFinish)
        {
            switch(type)
            {
                case NAT_Unknown:
                    myprintf("NAT Unknown\n");
                    break;
                case NAT_FullCone:
                    myprintf("NAT Full Cone\n");
                    break;
                case NAT_RestrictedCone:
                    myprintf("NAT Restricted Cone\n");
                    break;
                case NAT_PortRestrictedCone:
                    myprintf("NAT Port Restricted Cone\n");
                    break;
                case NAT_Symmetric:
                    myprintf("NAT Symmetric\n");
                    break;
            }
        }
    }

    ssize_t mySend(int sock,int ip, int port, const void*data, int len)
    {
        sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = ip;
        addr.sin_port = port;
        ssize_t ret = sendto(sock, data, len, 0, (sockaddr *) &addr, sizeof(sockaddr_in));
        if(ret<=0)
        {
            myprintf("send failed ret=%d err:%s\n", errno, strerror(errno));
        }
        return ret;
    }

    //从服务器向客户端发送数据
    bool subToClient(int ip,int port)
    {
        tagClientInfo info = {0};
        info.ipPort.ip = ip;
        info.ipPort.port = port;

        int sock = socket(AF_INET,SOCK_DGRAM,0);
        timeval to={0};
        to.tv_sec = 2;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&to, sizeof(to));

        char tempBuf[0x100]={0};
        tempBuf[0] = SCmd_SubToClinet;

        ssize_t ret = mySend(sock,info.ipPort.ip,info.ipPort.port,tempBuf,0x10);

        if(ret <=0 )
        {
            close(sock);
            return false;
        }
        sockaddr_in rcvAddr = {0};
        socklen_t  sockLen = sizeof(sockaddr_in);
        ret = recvfrom(sock,tempBuf,0x10,0, (sockaddr*)&rcvAddr, &sockLen);
        bool bSuccess =  false;
        if(ret>0)
        {
            if(SCmd_SubToClinetResponse == tempBuf[0])//从服务器收到客户端响应包, 则向主服务器反馈结果.
            {
                bSuccess = true;
            }
        }
        close(sock);
        return bSuccess;
    }

    void NotifySubNeedToClient(int ip, int port)
    {
        char buf[0x100] ={0};
        buf[0] = SCmd_SubNeedToClient;
        *(int*)&buf[1]=ip;
        *(u_short*)&buf[5]=port;

        in_addr client = {0};
        client.s_addr = ip;
        myprintf("Main To Sub(%s:%d) to connect client %s:%d\n",mSubServerIP,mSubServerPort, inet_ntoa(client), ntohs(port));
        mySend(mSock, inet_addr(mSubServerIP), htons(mSubServerPort), buf, 0x10);
    }

    void ResponseSubNeedToClient(int ip, int port, bool bSuccess)
    {
        char buf[0x20]={0};
        buf[0] = SCmd_SubNeedToClientResponse;
        *(int*)&buf[1] = ip;
        *(u_short*)&buf[5] = (u_short )port;
        buf[7] = (char)(bSuccess?1:0);

        mySend(mSock,mNotifyServerIP,mNotifyServerPort,buf, 0x10);
    }



};


class CClientCheckNAT
{
private:
    int mSock;
    char mServerIP[0x20];
    u_short mServerPort;
public:
    CClientCheckNAT()
    {
        mSock = socket(AF_INET,SOCK_DGRAM,0);
        timeval to={0};
        to.tv_sec = 10;
        setsockopt(mSock, SOL_SOCKET, SO_RCVTIMEO, (char*)&to, sizeof(to));
        //strcpy(mServerIP,"192.168.3.39");
        strcpy(mServerIP,"123.147.223.124");
        mServerPort = 18902;
        loadConfig();
        srand((u_int)time(NULL));
    }
    ~CClientCheckNAT()
    {
        close(mSock);
    }

    void setServer(const char*ip, u_short port)
    {
        strcpy(mServerIP, ip);
        mServerPort = port;
    }

    void loadConfig()
    {
        FILE *pFile = fopen("config.cfg","rb");
        if(pFile)
        {
            char buf[0x100]={0};
            fread(buf,1,0x100,pFile);
            fclose(pFile);
            char delim[]=", \t\r\n";
            char ip[0x20]={0};
            char port[0x10]={0};
            char* res[2] = {ip,port};
            int i = 0;
            char *pCh = strtok(buf,delim);
            while(pCh&&i<2)
            {
                strcpy(res[i],pCh);
                pCh = strtok(NULL,delim);
                ++i;
            }
            strcpy(mServerIP, ip);
            mServerPort = (u_short)atoi(port);
        }
    }

    ssize_t  bindPort()
    {
        sockaddr_in srvAddr = {0};
        srvAddr.sin_addr.s_addr = INADDR_ANY;
        srvAddr.sin_family = AF_INET;

        int tryTime = 10;
        int port = rand()%20000+9000;
        srvAddr.sin_port = htons(port);
        ssize_t ret = bind(mSock, (sockaddr *) &srvAddr, sizeof(sockaddr_in));
        while(ret != 0 && tryTime>0)
        {
            port = rand()%20000+9000;
            srvAddr.sin_port = htons(port);
            ret = bind(mSock, (sockaddr *) &srvAddr, sizeof(sockaddr_in));
            --tryTime;
        }
        myprintf("client bind port:%d\n", port);
        return ret;
    }

    int checkTypeOfNAT()
    {
        myprintf("checkTypeOfNAT begin\n");
        int type = NAT_Unknown;
        ssize_t  ret = bindPort();
        if(ret != 0)
        {
            return type;
        }

        char buf[0x100]={0};
        buf[0] = SCmd_ClientCmd;
        myprintf("send client cmd\n");
        ret = mySend(mSock, inet_addr(mServerIP), htons(mServerPort), buf,0x10);
        sockaddr_in rcvAddr = {0};
        socklen_t sockLen = sizeof(sockaddr_in);
        bool bFinish = false;

        while(bFinish==false)
        {
            sockLen = sizeof(sockaddr_in);
            ret = recvfrom(mSock, buf, 0x20,0,(sockaddr*)&rcvAddr, &sockLen);
            if(ret<=0)
            {
                myprintf("recv failed ret=%d err:%s\n", errno, strerror(errno));
                break;
            }
            switch(buf[0])
            {
                case SCmd_ClientCmdToSub:
                    myprintf("recv client cmd to sub\n");
                    CmdToSubServer(buf, ret);
                    break;
                case SCmd_ClientCmdFinish:
                    bFinish = true;
                    type = buf[1];
                    myprintf("recv client cmd finish type:%d\n", type);
                    break;
                case SCmd_SubToClinet:
                    myprintf("recv sub to client\n");
                    responseSubToClient(rcvAddr.sin_addr.s_addr, rcvAddr.sin_port);
                    break;
                default:
                    myprintf("unknown CMD type:%d\n", buf[0]);
                    break;
            }
        }
        myprintf("checkTypeOfNAT end\n");
        return type;
    }

    void CmdToSubServer(char *data, int len)
    {
        char buf[0x20]={0};
        buf[0] = SCmd_ClientCmdSub;
        memcpy(&buf[1], &data[2],6); //客户端连接到主服务器时用的IP和port
        int ip = *(int*)&data[8]; //从服务器ip和port
        u_short port = *(u_short*)&data[12];
        mySend(mSock, ip, port,buf,0x10); //向从服务器发命令包

    }

    void responseSubToClient(int ip, int port)
    {
        myprintf("Sub server cmd\n");
        char buf[0x10]={0};
        buf[0] = SCmd_SubToClinetResponse;
        mySend(mSock, ip, port,buf,0x10);
    }

    ssize_t mySend(int sock,int ip, int port, const void*data, int len)
    {
        sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = ip;
        addr.sin_port = port;
        ssize_t ret = sendto(sock, data, len, 0, (sockaddr *) &addr, sizeof(sockaddr_in));
        if(ret<=0)
        {
            myprintf("send failed ret=%d err:%s\n", errno, strerror(errno));
        }
        return ret;
    }
};

int ClientNAT(const char*ip, const char*port)
{
    CClientCheckNAT client;
    client.setServer(ip,(u_short )atoi(port));
    return client.checkTypeOfNAT();
}

#ifndef __ANDROID__
int main()
{
    CServerCheckNATMain srv;
    srv.startServer();
    return 0;
}
#endif