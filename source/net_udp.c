/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "q_stdinc.h"
#include "arch_def.h"
#include "net_sys.h"
#include "quakedef.h"
#include "net_defs.h"

#ifdef VITA
#include <vitasdk.h>
static void *net_memory = NULL;
#define NET_INIT_SIZE 1*1024*1024
#endif

#ifdef __SWITCH__
#include <fcntl.h>  // For fcntl() to set O_NONBLOCK on Switch
#include <switch.h>
#include <switch/services/ldn.h>
static bool ldn_initialized = false;
static bool ldn_connected = false;
static bool ldn_is_host = false;        // Track if we're the host or client
static bool ldn_station_open = false;   // Track if station is open for scanning
static u64 switch_current_title_id = 0; // Will hold the dynamically detected ID
static u32 switch_host_ip = 0;          // Will hold the host's virtual IP
static in_addr_t ldn_my_addr = 0;       // Our virtual IP on the LDN network

// unique LDN id so we only match NZP lobbies, not other games' sessions; must match on both sides
#define NZP_LDN_COMM_ID 0x4E5A50000000ULL  // 'NZP' in the high bytes
#endif

static sys_socket_t net_acceptsocket = INVALID_SOCKET;	// socket for fielding new connections
static sys_socket_t net_controlsocket;
static sys_socket_t net_broadcastsocket = 0;
static struct sockaddr_in broadcastaddr;

static in_addr_t	myAddr;

#include "net_udp.h"

//=============================================================================

#ifdef __SWITCH__
// LDN stores IPs as u32 in host byte order - convert to network byte order for sockets
static in_addr_t LdnIpToNetworkOrder(void *ldn_ip_ptr) {
    return htonl(*(u32*)ldn_ip_ptr);
}

// Print IP from LDN format (host byte order u32) correctly
static void PrintLdnIp(const char *label, void *ldn_ip_ptr) {
    u32 ip = *(u32*)ldn_ip_ptr;
    Con_Printf("%s%d.%d.%d.%d", label,
        (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff);
}

// Rebind a socket to the LDN IP address so Citron routes LDN traffic to it
static void Switch_RebindSocketToLdn(sys_socket_t *sock, int port) {
    if (*sock == INVALID_SOCKET || ldn_my_addr == 0) return;

    // Close old socket
    closesocket(*sock);

    // Create new socket
    sys_socket_t newsocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (newsocket == INVALID_SOCKET) {
        Con_Printf("LDN: Failed to create new socket for rebind\n");
        *sock = INVALID_SOCKET;
        return;
    }

    // Set non-blocking
    int flags = fcntl(newsocket, F_GETFL, 0);
    if (flags != -1) fcntl(newsocket, F_SETFL, flags | O_NONBLOCK);

    // Bind to LDN IP
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = ldn_my_addr;  // Bind to LDN IP, not INADDR_ANY
    address.sin_port = htons((unsigned short)port);

    if (bind(newsocket, (struct sockaddr *)&address, sizeof(address)) != 0) {
        Con_Printf("LDN: Failed to bind socket to LDN IP\n");
        closesocket(newsocket);
        *sock = INVALID_SOCKET;
        return;
    }

    *sock = newsocket;
    u32 hip = ntohl(ldn_my_addr);
    Con_Printf("LDN: Socket rebound to %d.%d.%d.%d:%d\n",
        (hip >> 24) & 0xff, (hip >> 16) & 0xff,
        (hip >> 8) & 0xff, hip & 0xff, port);
}

// Update myAddr with our LDN virtual IP - bypasses stubbed getsockname
static void Switch_UpdateMyAddr(void) {
    LdnIpv4Address ldn_ip = {0};
    LdnSubnetMask ldn_mask = {0};

    Result rc = ldnGetIpv4Address(&ldn_ip, &ldn_mask);
    if (R_SUCCEEDED(rc)) {
        // Convert from LDN format (host byte order) to network byte order for sockets
        ldn_my_addr = LdnIpToNetworkOrder(&ldn_ip);
        myAddr = ldn_my_addr;

        // Update broadcast address for LDN subnet
        in_addr_t mask_net = LdnIpToNetworkOrder(&ldn_mask);
        broadcastaddr.sin_addr.s_addr = (ldn_my_addr & mask_net) | ~mask_net;

        // Print IPs correctly from LDN's host byte order format
        PrintLdnIp("LDN: Virtual IP = ", &ldn_ip);
        Con_Printf(", Broadcast = ");
        // Broadcast is now in network order, convert back for display
        u32 bc_host = ntohl(broadcastaddr.sin_addr.s_addr);
        Con_Printf("%d.%d.%d.%d\n",
            (bc_host >> 24) & 0xff, (bc_host >> 16) & 0xff,
            (bc_host >> 8) & 0xff, bc_host & 0xff);
    } else {
        Con_Printf("LDN: ldnGetIpv4Address failed (Error: 0x%x)\n", rc);
    }
}

void Switch_DestroyLobby(void);   // forward decl (defined below)

void Switch_CreateLobby(void) {
    if (!ldn_initialized) return;
    if (ldn_connected) {
        if (ldn_is_host) return;  // already hosting, nothing to do
        // tear down a stale client connection from a previous join so we can host
        // (otherwise an instance that joined someone can never host afterwards).
        Con_Printf("LDN: Resetting stale client connection before hosting.\n");
        Switch_DestroyLobby();
    }
    // Close any lingering scan station too.
    if (ldn_station_open) {
        ldnCloseStation();
        ldn_station_open = false;
    }

    // Transition host state to Access Point
    Result rc = ldnOpenAccessPoint();
    if (R_FAILED(rc)) {
        Con_Printf("LDN: ldnOpenAccessPoint failed (Error: 0x%x)\n", rc);
        return;
    }

    LdnSecurityConfig sec_config = {0};
    LdnUserConfig user_config = {0};
    LdnNetworkConfig network_config = {0};

    sec_config.security_mode = LdnSecurityMode_Any;
    strcpy(user_config.user_name, "NZP Host");

    // Advertise our unique NZP id so clients can distinguish us from retail-game
    // LDN sessions on the same relay. Must match the client-side filter below.
    network_config.intent_id.local_communication_id = NZP_LDN_COMM_ID;
    network_config.channel = 0;
    network_config.node_count_max = 4;

    Con_Printf("LDN: Creating network (Title ID: %016llX)...\n",
        (unsigned long long)switch_current_title_id);

    rc = ldnCreateNetwork(&sec_config, &user_config, &network_config);
    if (R_FAILED(rc)) {
        Con_Printf("LDN: ldnCreateNetwork failed (Error: 0x%x)\n", rc);
        ldnCloseAccessPoint(); // Revert state back to Initialized on failure
    } else {
        Con_Printf("LDN: Successfully hosted lobby!\n");
        ldn_connected = true;
        ldn_is_host = true;

        // Update myAddr to our virtual LDN IP
        Switch_UpdateMyAddr();

        // Rebind the accept socket to LDN IP so Citron routes traffic correctly
        if (net_acceptsocket != INVALID_SOCKET) {
            Switch_RebindSocketToLdn(&net_acceptsocket, net_hostport);
        }
    }
}

void Switch_DestroyLobby(void) {
    if (!ldn_initialized || !ldn_connected) return;

    if (ldn_is_host) {
        ldnDestroyNetwork();
        ldnCloseAccessPoint();
    } else {
        ldnDisconnect();
        ldnCloseStation();
        ldn_station_open = false;
    }

    ldn_connected = false;
    ldn_is_host = false;
    ldn_my_addr = 0;
    switch_host_ip = 0;

    // Reset broadcast to default
    broadcastaddr.sin_addr.s_addr = INADDR_BROADCAST;

    Con_Printf("LDN: Disconnected from network.\n");
}

void Switch_ScanAndJoinLobby(void) {
    if (!ldn_initialized) return;
    if (ldn_connected) {
        if (!ldn_is_host) return;  // already joined a lobby, don't re-scan
        // Stale HOST network left over -- tear down so we can join someone else.
        Con_Printf("LDN: Resetting stale host network before joining.\n");
        Switch_DestroyLobby();
    }

    // Only open station once - keep it open for subsequent scans
    if (!ldn_station_open) {
        Result rc = ldnOpenStation();
        if (R_FAILED(rc)) {
            Con_Printf("LDN: ldnOpenStation failed (Error: 0x%x)\n", rc);
            return;
        }
        ldn_station_open = true;
        Con_Printf("LDN: Station opened, ready to scan.\n");
    }

    LdnScanFilter filter = {0};
    filter.flags = 0; // Scan for all lobbies without filters

    LdnNetworkInfo networks[8] = {0};
    s32 total_out = 0;

    Result rc = ldnScan(0, &filter, networks, 8, &total_out);
    if (R_FAILED(rc)) {
        Con_Printf("LDN: Scan failed (Error: 0x%x)\n", rc);
        return;  // Don't close station, try again next time
    }

    Con_Printf("LDN: Scan found %d network(s)\n", total_out);

    if (total_out > 0) {
        // Find the NZP network: prefer our unique magic id, but accept a legacy
        // local_comm_id==0 host as a fallback so old + new builds interoperate.
        s32 nzp_network = -1;
        s32 fallback_network = -1;
        for (s32 i = 0; i < total_out; i++) {
            u64 cid = networks[i].network_id.intent_id.local_communication_id;
            Con_Printf("LDN: Network %d: node_count=%d, local_comm_id=%016llX\n",
                i, networks[i].node_count, (unsigned long long)cid);

            if (cid == NZP_LDN_COMM_ID) {
                nzp_network = i;
                break;  // exact match, stop looking
            }
            if (cid == 0 && fallback_network < 0) {
                fallback_network = i;  // legacy host, remember but keep scanning
            }
        }

        if (nzp_network < 0)
            nzp_network = fallback_network;

        if (nzp_network < 0) {
            Con_Printf("LDN: No NZP lobby found (need local_comm_id=%016llX or 0)\n",
                (unsigned long long)NZP_LDN_COMM_ID);
            return;
        }

        Con_Printf("LDN: Connecting to NZP network %d...\n", nzp_network);

        LdnSecurityConfig sec_config = {0};
        sec_config.security_mode = LdnSecurityMode_Any;

        LdnUserConfig user_config = {0};
        strcpy(user_config.user_name, "NZP Client");

        rc = ldnConnect(&sec_config, &user_config, 1, 0, &networks[nzp_network]);
        if (R_FAILED(rc)) {
            Con_Printf("LDN: ldnConnect failed (Error: 0x%x)\n", rc);
            // Don't close station, might be transient failure
        } else {
            ldn_connected = true;
            ldn_is_host = false;
            ldn_station_open = false;  // Station becomes "connected" state

            // Extract host's IP from node 0 (the host is always node 0)
            // Convert to network byte order for socket operations
            switch_host_ip = LdnIpToNetworkOrder(&networks[nzp_network].nodes[0].ip_addr);

            // Print using LDN's host byte order format
            PrintLdnIp("LDN: Connected to host at ", &networks[nzp_network].nodes[0].ip_addr);
            Con_Printf("\n");

            // Update myAddr to our virtual LDN IP
            Switch_UpdateMyAddr();

            // Rebind control socket to LDN IP so responses come back correctly
            Switch_RebindSocketToLdn(&net_controlsocket, 0);  // Port 0 = ephemeral
        }
    }
}

// Allow manual scan trigger from console
void Switch_CloseScanStation(void) {
    if (ldn_station_open && !ldn_connected) {
        ldnCloseStation();
        ldn_station_open = false;
        Con_Printf("LDN: Station closed.\n");
    }
}
#endif

//=============================================================================

sys_socket_t UDP_Init (void)
{
	int	err;
	char	*tst;
	char	buff[MAXHOSTNAMELEN];
	struct qsockaddr	addr;
#ifndef VITA
#ifndef __SWITCH__
	struct hostent *local;
#endif
#endif

#ifdef __SWITCH__
	if (!ldn_initialized) {
		Result ldn_rc = ldnInitialize(LdnServiceType_User);
		if (R_SUCCEEDED(ldn_rc)) {
			ldn_initialized = true;
			Con_SafePrintf("LDN: Service initialized successfully!\n");

			// Query the emulator dynamically for our currently assigned Title ID (Program ID)
			u64 program_id = 0;
			Result info_rc = svcGetInfo(&program_id, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0);
			if (R_SUCCEEDED(info_rc)) {
				switch_current_title_id = program_id;
				Con_SafePrintf("LDN: Detected running Title ID as %016llX\n", (unsigned long long)switch_current_title_id);
			} else {
				switch_current_title_id = 0; // Fallback if query fails
				Con_SafePrintf("LDN: Warning, failed to query running Title ID, falling back to 0\n");
			}
		} else {
			Con_SafePrintf("LDN: Failed to initialize (Error: 0x%x)\n", ldn_rc);
		}
	}
#endif

	if (COM_CheckParm ("-noudp"))
		return INVALID_SOCKET;

	// determine my name & address
#ifndef VITA
	myAddr = htonl(INADDR_LOOPBACK);
#endif
#ifdef VITA
	// Start SceNet & SceNetCtl
	SceNetInitParam initparam;
	int ret = sceNetShowNetstat();
	if (ret == SCE_NET_ERROR_ENOTINIT) {
		net_memory = malloc(NET_INIT_SIZE);

		initparam.memory = net_memory;
		initparam.size = NET_INIT_SIZE;
		initparam.flags = 0;

		ret = sceNetInit(&initparam);
		if (ret < 0) return -1;

		ret = sceNetCtlInit();
		if (ret < 0){
			sceNetTerm();
			free(net_memory);
			return -1;
		}
	}

	// Getting IP address
	SceNetCtlInfo info;
	sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info);
	sceNetInetPton(SCE_NET_AF_INET, info.ip_address, &myAddr);

	// if the quake hostname isn't set, set it to player nickname
	if (!strcmp(hostname.string, "UNNAMED"))
	{
		SceAppUtilInitParam init_param;
		SceAppUtilBootParam boot_param;
		memset(&init_param, 0, sizeof(SceAppUtilInitParam));
		memset(&boot_param, 0, sizeof(SceAppUtilBootParam));
		sceAppUtilInit(&init_param, &boot_param);
		SceChar8 nick[SCE_SYSTEM_PARAM_USERNAME_MAXSIZE];
		sceAppUtilSystemParamGetString(SCE_SYSTEM_PARAM_ID_USERNAME, nick, SCE_SYSTEM_PARAM_USERNAME_MAXSIZE);
		Cvar_Set ("hostname", (const char*)nick);
	}
#else
	if (gethostname(buff, MAXHOSTNAMELEN) != 0)
	{
		err = SOCKETERRNO;
		Con_SafePrintf("UDP_Init: gethostname failed (%s)\n",
							socketerror(err));
	}
	else
	{
		buff[MAXHOSTNAMELEN - 1] = 0;
#ifdef __SWITCH__
		// gethostid() returns 127.0.0.1 in the wrong byteorder if we're not
		// connected to the internet, otherwise our local IP in correct order
		myAddr = gethostid();
		if (myAddr == 0x7f000001)
			myAddr = htonl(myAddr);
#else
#ifdef PLATFORM_OSX
		// ericw -- if our hostname ends in ".local" (a macOS thing),
		// don't bother calling gethostbyname(), because it blocks for a few seconds
		// and then fails (on my system anyway.)
		tst = strstr(buff, ".local");
		if (tst && tst[6] == '\0')
		{
			Con_SafePrintf("UDP_Init: skipping gethostbyname for %s\n", buff);
		}
		else
#endif
		if (!(local = gethostbyname(buff)))
		{
			Con_SafePrintf("UDP_Init: gethostbyname failed (%s)\n",
							hstrerror(h_errno));
		}
		else if (local->h_addrtype != AF_INET)
		{
			Con_SafePrintf("UDP_Init: address from gethostbyname not IPv4\n");
		}
		else
		{
			myAddr = *(in_addr_t *)local->h_addr_list[0];
		}
#endif
	}
#endif
	if ((net_controlsocket = UDP_OpenSocket(0)) == INVALID_SOCKET)
	{
		Con_SafePrintf("UDP_Init: Unable to open control socket, UDP disabled\n");
		return INVALID_SOCKET;
	}

	broadcastaddr.sin_family = AF_INET;
	broadcastaddr.sin_addr.s_addr = INADDR_BROADCAST;
	broadcastaddr.sin_port = htons((unsigned short)net_hostport);

	UDP_GetSocketAddr (net_controlsocket, &addr);
	strcpy(my_tcpip_address, UDP_AddrToString (&addr));
	tst = strrchr(my_tcpip_address, ':');
	if (tst) *tst = 0;

	Con_SafePrintf("UDP Initialized\n");
	tcpipAvailable = true;

	return net_controlsocket;
}

//=============================================================================

void UDP_Shutdown (void)
{
	UDP_Listen (false);
	UDP_CloseSocket (net_controlsocket);
#ifdef __SWITCH__
	// Clean up LDN state
	if (ldn_connected) {
		Switch_DestroyLobby();
	}
	if (ldn_station_open) {
		ldnCloseStation();
		ldn_station_open = false;
	}
	if (ldn_initialized) {
		ldnExit();
		ldn_initialized = false;
	}
#endif
}

//=============================================================================

void UDP_Listen (qboolean state)
{
	// enable listening
	if (state)
	{
		if (net_acceptsocket != INVALID_SOCKET)
			return;
		if ((net_acceptsocket = UDP_OpenSocket (net_hostport)) == INVALID_SOCKET)
			Sys_Error ("UDP_Listen: Unable to open accept socket");
#ifdef __SWITCH__
		Switch_CreateLobby();
#endif
		return;
	}

	// disable listening
	if (net_acceptsocket == INVALID_SOCKET)
		return;
#ifdef __SWITCH__
	Switch_DestroyLobby();
#endif
	UDP_CloseSocket (net_acceptsocket);
	net_acceptsocket = INVALID_SOCKET;
}

//=============================================================================

sys_socket_t UDP_OpenSocket (int port)
{
	sys_socket_t newsocket;
	struct sockaddr_in address;
	int err;

	if ((newsocket = socket (PF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET)
	{
		err = SOCKETERRNO;
		Con_SafePrintf("UDP_OpenSocket: %s\n", socketerror(err));
		return INVALID_SOCKET;
	}
#ifdef VITA
	{
		int _true = 1;
		if (setsockopt(newsocket, SOL_SOCKET, SCE_NET_SO_NBIO, (char *)&_true, sizeof(uint32_t)) == -1)
			goto ErrorReturn;
	}
#elif defined(__SWITCH__)
	// Switch/Citron doesn't fully support ioctl, use fcntl instead
	{
		int flags = fcntl(newsocket, F_GETFL, 0);
		if (flags == -1 || fcntl(newsocket, F_SETFL, flags | O_NONBLOCK) == -1)
			goto ErrorReturn;
	}
	if (0)  // Skip the error check below since we handled it above
#else
	{
		int _true = 1;
		if (ioctlsocket (newsocket, FIONBIO, &_true) == SOCKET_ERROR)
			goto ErrorReturn;
	}
#endif
		;

	memset(&address, 0, sizeof(struct sockaddr_in));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons((unsigned short)port);
	if (bind (newsocket, (struct sockaddr *)&address, sizeof(address)) == 0)
		return newsocket;

ErrorReturn:
	err = SOCKETERRNO;
	Con_SafePrintf("UDP_OpenSocket: %s\n", socketerror(err));
	UDP_CloseSocket (newsocket);
	return INVALID_SOCKET;
}

//=============================================================================

int UDP_CloseSocket (sys_socket_t socketid)
{
	if (socketid == net_broadcastsocket)
		net_broadcastsocket = 0;
	return closesocket (socketid);
}

//=============================================================================

/*
============
PartialIPAddress

this lets you type only as much of the net address as required, using
the local network components to fill in the rest
============
*/
static int PartialIPAddress (const char *in, struct qsockaddr *hostaddr)
{
	char	buff[256];
	char	*b;
	int	addr, mask, num, port, run;

	buff[0] = '.';
	b = buff;
	strcpy(buff+1, in);
	if (buff[1] == '.')
		b++;

	addr = 0;
	mask = -1;
	while (*b == '.')
	{
		b++;
		num = 0;
		run = 0;
		while (!( *b < '0' || *b > '9'))
		{
			num = num*10 + *b++ - '0';
			if (++run > 3)
				return -1;
		}
		if ((*b < '0' || *b > '9') && *b != '.' && *b != ':' && *b != 0)
			return -1;
		if (num < 0 || num > 255)
			return -1;
		mask <<= 8;
		addr = (addr<<8) + num;
	}

	if (*b++ == ':')
		port = atoi(b);
	else
		port = net_hostport;

	hostaddr->qsa_family = AF_INET;
	((struct sockaddr_in *)hostaddr)->sin_port = htons((unsigned short)port);
	((struct sockaddr_in *)hostaddr)->sin_addr.s_addr =
					(myAddr & htonl(mask)) | htonl(addr);

	return 0;
}

//=============================================================================

int UDP_Connect (sys_socket_t socketid, struct qsockaddr *addr)
{
	return 0;
}

//=============================================================================

sys_socket_t UDP_CheckNewConnections (void)
{
	struct sockaddr_in	from;
	socklen_t	fromlen = sizeof(struct sockaddr_in);
	char		buff[1];

	if (net_acceptsocket == INVALID_SOCKET)
		return INVALID_SOCKET;
#if defined(VITA) || defined(__SWITCH__)
	// VITA and Switch/Citron don't support ioctl(FIONREAD), use recvfrom with MSG_PEEK
	{
		char buf[4096];
		if (recvfrom(net_acceptsocket, buf, sizeof(buf), MSG_PEEK, NULL, NULL) >= 0)
			return net_acceptsocket;
	}
#else
	{
		int available;
		if (ioctl (net_acceptsocket, FIONREAD, &available) == -1)
		{
			int err = SOCKETERRNO;
			Sys_Error ("UDP: ioctlsocket (FIONREAD) failed (%s)", socketerror(err));
		}
		if (available)
			return net_acceptsocket;
	}
#endif
	// quietly absorb empty packets
	recvfrom (net_acceptsocket, buff, 0, 0, (struct sockaddr *) &from, &fromlen);
	return INVALID_SOCKET;
}

//=============================================================================

int UDP_Read (sys_socket_t socketid, byte *buf, int len, struct qsockaddr *addr)
{
	socklen_t addrlen = sizeof(struct qsockaddr);
	int ret;

	ret = recvfrom (socketid, buf, len, 0, (struct sockaddr *)addr, &addrlen);
	if (ret == SOCKET_ERROR)
	{
		int err = SOCKETERRNO;
		if (err == NET_EWOULDBLOCK || err == NET_ECONNREFUSED)
			return 0;
		Con_SafePrintf ("UDP_Read, recvfrom: %s\n", socketerror(err));
	}
	return ret;
}

//=============================================================================

static int UDP_MakeSocketBroadcastCapable (sys_socket_t socketid)
{
	int	i = 1;

	// make this socket broadcast capable
	if (setsockopt(socketid, SOL_SOCKET, SO_BROADCAST, (char *)&i, sizeof(i))
								 == SOCKET_ERROR)
	{
		int err = SOCKETERRNO;
		Con_SafePrintf ("UDP, setsockopt: %s\n", socketerror(err));
		return -1;
	}
	net_broadcastsocket = socketid;

	return 0;
}

//=============================================================================

int UDP_Broadcast (sys_socket_t socketid, byte *buf, int len)
{
	int	ret;

#ifdef __SWITCH__
	// Scan and establish connection BEFORE the engine broadcasts packets
	Switch_ScanAndJoinLobby();
#endif

	if (socketid != net_broadcastsocket)
	{
		if (net_broadcastsocket != 0)
			Sys_Error("Attempted to use multiple broadcasts sockets");
		ret = UDP_MakeSocketBroadcastCapable (socketid);
		if (ret == -1)
		{
			Con_Printf("Unable to make socket broadcast capable\n");
			return ret;
		}
	}

#ifdef __SWITCH__
	// If connected over LDN, send directly to host's virtual IP instead of broadcast
	if (ldn_connected && switch_host_ip != 0)
	{
		struct sockaddr_in direct_addr;
		memset(&direct_addr, 0, sizeof(direct_addr));
		direct_addr.sin_family = AF_INET;
		direct_addr.sin_addr.s_addr = switch_host_ip;
		direct_addr.sin_port = htons((unsigned short)net_hostport);
		{
			u32 h = ntohl(switch_host_ip);
			u32 m = ntohl(ldn_my_addr);
			// If host and my subnet differ, this unicast will never arrive.
			Con_SafePrintf("NETDBG: search send DIRECT -> %d.%d.%d.%d:%d  (me %d.%d.%d.%d) len=%d\n",
				(h>>24)&0xff,(h>>16)&0xff,(h>>8)&0xff,h&0xff, net_hostport,
				(m>>24)&0xff,(m>>16)&0xff,(m>>8)&0xff,m&0xff, len);
		}
		return UDP_Write(socketid, buf, len, (struct qsockaddr *)&direct_addr);
	}

	{
		u32 b = ntohl(broadcastaddr.sin_addr.s_addr);
		Con_SafePrintf("NETDBG: search send BROADCAST -> %d.%d.%d.%d:%d len=%d (ldn_connected=%d)\n",
			(b>>24)&0xff,(b>>16)&0xff,(b>>8)&0xff,b&0xff, net_hostport, len, (int)ldn_connected);
	}
#endif

	return UDP_Write (socketid, buf, len, (struct qsockaddr *)&broadcastaddr);
}

//=============================================================================

int UDP_Write (sys_socket_t socketid, byte *buf, int len, struct qsockaddr *addr)
{
	int	ret;

	ret = sendto (socketid, buf, len, 0, (struct sockaddr *)addr,
							sizeof(struct qsockaddr));
	if (ret == SOCKET_ERROR)
	{
		int err = SOCKETERRNO;
		if (err == NET_EWOULDBLOCK)
			return 0;
		Con_SafePrintf ("UDP_Write, sendto: %s\n", socketerror(err));
	}
	return ret;
}

//=============================================================================

const char *UDP_AddrToString (struct qsockaddr *addr)
{
	static char buffer[22];
	int		haddr;

	haddr = ntohl(((struct sockaddr_in *)addr)->sin_addr.s_addr);
	q_snprintf (buffer, sizeof(buffer), "%d.%d.%d.%d:%d", (haddr >> 24) & 0xff,
			   (haddr >> 16) & 0xff, (haddr >> 8) & 0xff, haddr & 0xff,
			    ntohs(((struct sockaddr_in *)addr)->sin_port));
	return buffer;
}

//=============================================================================

int UDP_StringToAddr (const char *string, struct qsockaddr *addr)
{
	int	ha1, ha2, ha3, ha4, hp, ipaddr;

	sscanf(string, "%d.%d.%d.%d:%d", &ha1, &ha2, &ha3, &ha4, &hp);
	ipaddr = (ha1 << 24) | (ha2 << 16) | (ha3 << 8) | ha4;

	addr->qsa_family = AF_INET;
	((struct sockaddr_in *)addr)->sin_addr.s_addr = htonl(ipaddr);
	((struct sockaddr_in *)addr)->sin_port = htons((unsigned short)hp);
	return 0;
}

//=============================================================================

int UDP_GetSocketAddr (sys_socket_t socketid, struct qsockaddr *addr)
{
	socklen_t addrlen = sizeof(struct qsockaddr);
	in_addr_t a;

	memset(addr, 0, sizeof(struct qsockaddr));
	if (getsockname(socketid, (struct sockaddr *)addr, &addrlen) != 0)
		return -1;

	a = ((struct sockaddr_in *)addr)->sin_addr.s_addr;
	if (a == 0 || a == htonl(INADDR_LOOPBACK))
		((struct sockaddr_in *)addr)->sin_addr.s_addr = myAddr;

	return 0;
}

//=============================================================================

int UDP_GetNameFromAddr (struct qsockaddr *addr, char *name)
{
	struct hostent *hostentry;

	hostentry = gethostbyaddr ((char *)&((struct sockaddr_in *)addr)->sin_addr,
						sizeof(struct in_addr), AF_INET);
	if (hostentry)
	{
		strncpy (name, (char *)hostentry->h_name, NET_NAMELEN - 1);
		return 0;
	}

	strcpy (name, UDP_AddrToString (addr));
	return 0;
}

//=============================================================================

int UDP_GetAddrFromName (const char *name, struct qsockaddr *addr)
{
	struct hostent *hostentry;

	if (name[0] >= '0' && name[0] <= '9')
		return PartialIPAddress (name, addr);

	hostentry = gethostbyname (name);
	if (!hostentry)
		return -1;

	addr->qsa_family = AF_INET;
	((struct sockaddr_in *)addr)->sin_port = htons((unsigned short)net_hostport);
	((struct sockaddr_in *)addr)->sin_addr.s_addr =
						*(in_addr_t *)hostentry->h_addr_list[0];

	return 0;
}

//=============================================================================

int UDP_AddrCompare (struct qsockaddr *addr1, struct qsockaddr *addr2)
{
	if (addr1->qsa_family != addr2->qsa_family)
		return -1;

	if (((struct sockaddr_in *)addr1)->sin_addr.s_addr !=
	    ((struct sockaddr_in *)addr2)->sin_addr.s_addr)
		return -1;

	if (((struct sockaddr_in *)addr1)->sin_port !=
	    ((struct sockaddr_in *)addr2)->sin_port)
		return 1;

	return 0;
}

//=============================================================================

int UDP_GetSocketPort (struct qsockaddr *addr)
{
	return ntohs(((struct sockaddr_in *)addr)->sin_port);
}


int UDP_SetSocketPort (struct qsockaddr *addr, int port)
{
	((struct sockaddr_in *)addr)->sin_port = htons((unsigned short)port);
	return 0;
}

//=============================================================================
