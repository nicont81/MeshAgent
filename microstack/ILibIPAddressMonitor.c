#ifdef WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <iphlpapi.h>
	#include <Dbghelp.h>
#endif
#include "ILibParsers.h"

#ifdef _POSIX
#include <netinet/in.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#endif

typedef void* ILibIPAddressMonitor;
typedef void(*ILibIPAddressMonitor_Handler)(ILibIPAddressMonitor sender, void *user);
typedef struct _ILibIPAddressMonitor
{
	ILibChain_Link chainLink;
	ILibIPAddressMonitor_Handler onUpdate;
	void *user;
#ifdef WIN32
	SOCKET mSocket;
	DWORD bytesReturned;
	WSAOVERLAPPED reserved;
#elif defined (_POSIX)
	int mSocket;
	struct sockaddr_nl addr;
#endif

}_ILibIPAddressMonitor;
int ILibMemory_IPAddressMonitor_CONTAINER_SIZE = sizeof(_ILibIPAddressMonitor);
void ILibIPAddressMonitor_MicrostackThreadDispatch(void *chain, void *user);

#ifdef WIN32
void CALLBACK ILibIPAddressMonitor_dispatch(
	IN DWORD dwError,
	IN DWORD cbTransferred,
	IN LPWSAOVERLAPPED lpOverlapped,
	IN DWORD dwFlags
)
{
	if (dwError == 0)
	{
		_ILibIPAddressMonitor *obj = (_ILibIPAddressMonitor*)lpOverlapped->hEvent;
		ILibChain_RunOnMicrostackThread(obj->chainLink.ParentChain, ILibIPAddressMonitor_MicrostackThreadDispatch, obj);
	}
}
void ILibIPAddressMonitor_MicrostackThreadDispatch(void *chain, void *user)
{
	_ILibIPAddressMonitor *obj = (_ILibIPAddressMonitor*)user;
	if (obj->onUpdate != NULL) { obj->onUpdate(obj, obj->user); }
	WSAIoctl(obj->mSocket, SIO_ADDRESS_LIST_CHANGE, NULL, 0, NULL, 0, &(obj->bytesReturned), &(obj->reserved), ILibIPAddressMonitor_dispatch);
}
#endif

void ILibIPAddressMonitor_Destroy(void *object)
{
	_ILibIPAddressMonitor *obj = (_ILibIPAddressMonitor*)object;

#ifdef WIN32
	closesocket(obj->mSocket);
	obj->mSocket = INVALID_SOCKET;
#elif defined(_POSIX)
	close(obj->mSocket);
	obj->mSocket = -1;
#endif
}

#if defined(_POSIX)
void ILibIPAddressMonitor_PreSelect(void* object, fd_set *readset, fd_set *writeset, fd_set *errorset, int* blocktime)
{
	_ILibIPAddressMonitor *obj = (_ILibIPAddressMonitor*)object;
	FD_SET(obj->mSocket, readset);
}
void ILibIPAddressMonitor_PostSelect(void* object, int slct, fd_set *readset, fd_set *writeset, fd_set *errorset)
{
	char buffer[4096];
	int len;
	int update = 0;
	struct nlmsghdr *nlh = (struct nlmsghdr *)buffer;

	_ILibIPAddressMonitor *obj = (_ILibIPAddressMonitor*)object;
	if (FD_ISSET(obj->mSocket, readset) != 0)
	{
		while ((len = recv(obj->mSocket, nlh, sizeof(buffer), 0)) > 0)
		{
			while ((NLMSG_OK(nlh, len)) && (nlh->nlmsg_type != NLMSG_DONE))
			{
				if (nlh->nlmsg_type == RTM_NEWADDR || nlh->nlmsg_type == RTM_DELADDR)
				{
					update = 1;
				}
				nlh = NLMSG_NEXT(nlh, len);
			}
		}

		if (update != 0 && obj->onUpdate != NULL) { obj->onUpdate(obj, obj->user); }
	}
}
#endif

ILibIPAddressMonitor ILibIPAddressMonitor_Create(void *chain, ILibIPAddressMonitor_Handler handler, void *user)
{
	_ILibIPAddressMonitor *obj = (_ILibIPAddressMonitor*)ILibChain_Link_Allocate(ILibMemory_IPAddressMonitor_CONTAINER_SIZE, 0);
	obj->onUpdate = handler;
	obj->user = user;
#ifdef WIN32
	obj->reserved.hEvent = (HANDLE)obj;
	obj->mSocket = socket(AF_INET, SOCK_DGRAM, 0);
	WSAIoctl(obj->mSocket, SIO_ADDRESS_LIST_CHANGE, NULL, 0, NULL, 0, &(obj->bytesReturned), &(obj->reserved), ILibIPAddressMonitor_dispatch);
#elif defined (_POSIX)
	obj->mSocket = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
	int flags = fcntl(obj->mSocket, F_GETFL, 0);
	fcntl(obj->mSocket, F_SETFL, O_NONBLOCK | flags);

	memset(&(obj->addr), 0, sizeof(obj->addr));
	obj->addr.nl_family = AF_NETLINK;
	obj->addr.nl_groups = RTMGRP_IPV4_IFADDR;

	if (bind(obj->mSocket, (struct sockaddr *)&(obj->addr), sizeof(obj->addr)) == -1) 
	{
		close(obj->mSocket);
		free(obj);
		return(NULL);
	}

	obj->chainLink.PreSelectHandler = ILibIPAddressMonitor_PreSelect;
	obj->chainLink.PostSelectHandler = ILibIPAddressMonitor_PostSelect;
#endif

	obj->chainLink.DestroyHandler = ILibIPAddressMonitor_Destroy;
	ILibAddToChain(chain, obj);
	return((ILibIPAddressMonitor)obj);
}