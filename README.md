# nat-type 判断NAT类型
判断NAT类型, NAT类型有常见的4种. Full Cone NAT, Restricted Cone NAT, Port Restricted Cone NAT, Symmetric NAT
#NAT类型特点描述
## **Full Cone NAT**:  
&ensp;&ensp;&ensp;&ensp;内网主机建立一个UDP socket(LocalIP:LocalPort) 第一次使用这个socket给外部主机发送数据时NAT会给其分配一个公网(PublicIP,PublicPort),以后用这个socket向外面**任何主机**发送数据都将使用这对(PublicIP,PublicPort)。此外**任何外部主机**只要知道这个(PublicIP,PublicPort)就可以发送数据给(PublicIP,PublicPort)，内网的主机就能收到这个数据包 
   
## **Restricted Cone NAT**: 
&ensp;&ensp;&ensp;&ensp;内网主机建立一个UDP socket(LocalIP,LocalPort) 第一次使用这个socket给外部主机发送数据时NAT会给其分配一个公网(PublicIP,PublicPort),以后用这个socket向外面**任何主机**发送数据都将使用这对(PublicIP,PublicPort)。此外，如果任何外部主机想要发送数据给这个内网主机，只要知道这个(PublicIP,PublicPort)并且内网主机之前用这个**socket曾向这个外部主机IP发送过数据**。只要满足这两个条件，这个外部主机就可以用自己的(**IP,任何端口**)发送数据给(PublicIP,PublicPort)，内网的主机就能收到这个数据包 
   
## **Port Restricted Cone NAT**:

&ensp;&ensp;&ensp;&ensp;内网主机建立一个UDP socket(LocalIP,LocalPort) 第一次使用这个socket给外部主机发送数据时NAT会给其分配一个公网(PublicIP,PublicPort),以后用这个socket向外面**任何主机**发送数据都将使用这对(PublicIP,PublicPort)。此外，如果任何外部主机想要发送数据给这个内网主机，只要知道这个(PublicIP,PublicPort)并且内网主机之前用这个**socket曾向这个外部主机(IP,Port)发送过数据**。只要满足这两个条件，这个外部主机就可以用自己的(**IP,Port**)发送数据给(PublicIP,PublicPort)，内网的主机就能收到这个数据包 
    
    
## **Symmetric NAT**: 
&ensp;&ensp;&ensp;&ensp;内网主机建立一个UDP socket(LocalIP,LocalPort),当用这个socket第一次发数据给外部主机1时,NAT为其映射一个(PublicIP-1,Port-1),以后内网主机发送给外部主机1的所有数据都是用这个(PublicIP-1,Port-1)，如果内网主机同时用这个socket给外部主机2发送数据，第一次发送时，NAT会为其分配一个(PublicIP-2,Port-2), 以后内网主机发送给外部主机2的所有数据都是用这个(PublicIP-2,Port-2).如果NAT有多于一个公网IP，则PublicIP-1和PublicIP-2可能不同，如果NAT只有一个公网IP,则Port-1和Port-2肯定不同，也就是说一定不能是PublicIP-1等于 PublicIP-2且Port-1等于Port-2。此外，如果任何外部主机想要发送数据给这个内网主机，那么它首先应该收到内网主机发给他的数据，然后才能往回发送，否则即使他知道内网主机的一个(PublicIP,Port)也不能发送数据给内网主机，这种NAT无法实现UDP-P2P通信。

&ensp;&ensp;&ensp;&ensp;==同一个socket向不同外部主机通信,会分配不同的IP和端口, 只有对应的目标主机IP和端口才能与之通信,非常严格==

#判断NAT类型的思路
综合上在的NAT类型的特点,那么至少需要两台辅助服务器.
##步骤
假设有两个辅助服务器:s1,s2
客户端:c1

1.  c1向S1发送命令, s1让s2连接c1, 如果s2能连通c1, 说明是 Full Cone NAT

2.  如果s2不能连通c1,则让c1用同一个socket向s2发命令. 然后s2收到c1命令后, 如果端口改变, 则是Symmetric NAT.  

3.  如果端口相同, s2使用另外的socket连接c1,如果能连通,说明是Restricted Cone NAT, 否则是Port Restricted NAT.
