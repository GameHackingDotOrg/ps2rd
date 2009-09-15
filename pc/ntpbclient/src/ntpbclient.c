/*
 * ntpbclient.c - PC side of remote debugger
 *
 * Copyright (C) 2009 jimmikaelkael <jimmikaelkael@wanadoo.fr>
 * Copyright (C) 2009 misfire <misfire@xploderfreax.de>
 *
 * This file is part of Artemis, the PS2 game debugger.
 *
 * Artemis is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Artemis is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Artemis.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#else
#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>

#define PROGRAM_VER "0.2"

#ifdef _WIN32
WSADATA *WsaData;
#define _closesocket	closesocket
#else
#define _closesocket	close
#endif

#define SERVER_TCP_PORT  		4234
#define SERVER_UDP_PORT  		4244

char g_server_ip[16] = "192.168.0.10";
unsigned char pktbuffer[65536];
char netlogbuffer[1024];

/* NTPB header magic */
#define ntpb_MagicSize  		6
const unsigned char ntpb_hdrMagic[6] = {'\xff', '\x00', 'N', 'T', 'P', 'B'};
#define ntpb_hdrSize  			10

static int main_socket = -1;

/* command line commands stuff */
#define MEMZONE_EE 			"EE"
#define MEMZONE_IOP 			"IOP"
#define MEMZONE_KERNEL 			"Kernel"
#define MEMZONE_SCRATCHPAD 		"ScratchPad"

/* Remote commands to be sent to server */
#define REMOTE_CMD_NONE			0x000
#define REMOTE_CMD_DUMPEE		0x101
#define REMOTE_CMD_DUMPIOP		0x102
#define REMOTE_CMD_DUMPKERNEL		0x103
#define REMOTE_CMD_DUMPSCRATCHPAD	0x104
#define REMOTE_CMD_HALT			0x201
#define REMOTE_CMD_RESUME		0x202

/* commands sent in return by server */
#define NTPBCMD_PRINT_EEDUMP 		0x301
#define NTPBCMD_PRINT_IOPDUMP		0x302
#define NTPBCMD_PRINT_KERNELDUMP 	0x303
#define NTPBCMD_PRINT_SCRATCHPADDUMP	0x304
#define NTPBCMD_END_TRANSMIT		0xffff

pthread_t netlog_thread_id;

/*
 * printVer - print program version
 */
void printVer(void)
{
	printf("ntpbclient version %s\n", PROGRAM_VER);
}

/*
 * printUsage - Usage screen print
 */
void printUsage(void)
{
	printf("Usage: ntpbclient <command> [ARGS] [OPTION]\n");
	printf("ntpbclient command-line version %s\n", PROGRAM_VER);
	printf("Supported commands:\n");
	printf("\t --dump, -D <memzone> <start_address> <end_address> <outfile>\n");
	printf("\t \t memzone = 'EE', 'IOP', 'Kernel', 'ScrathPad'\n");
	printf("\t\t\t\t\t Dump PS2 memory to a file\n");
	printf("\t --halt, -H\t\t\t Halt game execution\n");
	printf("\t --resume, -R\t\t\t Resume game execution\n");
	printf("\t --log, --printlog, -L\t\t Show log file\n");
	printf("\t --clearlog, -C\t\t\t Clears the log file\n");
	printf("\t --help, -h\t\t\t Print this help\n");
	printf("\t --version, -v\t\t\t Print program version\n");
	printf("Supported options:\n");
	printf("\t -ip IP_ADDRESS\t\t\t Use this IP to connect\n");
}

#ifdef _WIN32
/*
 * InitWS2 - Windows specific: Winsock init.
 */
WSADATA *InitWS2(void)
{
	int 	r;			/* catches return value of WSAStartup 	*/
	static 	WSADATA WsaData;	/* receives data from WSAStartup	*/
	int 	ret;			/* return value flag			*/

	ret = 1;

	/* Start WinSock 2.  If it fails, we don't need to call
	 * WSACleanup().
	 */
	r = WSAStartup(MAKEWORD(2, 0), &WsaData);

	if (r) {
		printf("error: can't find high enough version of WinSock\n");
		ret = 0;
	} else {
		/* Now confirm that the WinSock 2 DLL supports the exact version
		 * we want. If not, make sure to call WSACleanup().
		 */
		if ((WsaData.wVersion & 0xff) != 2) {
			printf("error: can't find the correct version of WinSock\n");
			WSACleanup();
			ret = 0;
		}
	}

	if (ret)
		return &WsaData;

	return NULL;
}
#endif

/*
 * clientConnect - connect to ntpbserver.
 */
int clientConnect(void)
{
	int r, tcp_socket, err;
	struct sockaddr_in peer;

	peer.sin_family = AF_INET;
	peer.sin_port = htons(SERVER_TCP_PORT);
	peer.sin_addr.s_addr = inet_addr(g_server_ip);

	tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (tcp_socket < 0) {
		err = -1;
		goto error;
	}

	r = connect(tcp_socket, (struct sockaddr*)&peer, sizeof(peer));
	if (r < 0) {
		err = -2;
		goto error;
	}

	main_socket = tcp_socket;

	return 0;

error:
	_closesocket(tcp_socket);

	return err;
}

/*
 * clientDisconnect - disconnect client from ntpbserver.
 */
int clientDisconnect(void)
{
	_closesocket(main_socket);

	return 0;
}

/*
 * HexaToDecimal - return decimal value from hex value passed as string
 */
unsigned int HexaToDecimal(const char* pszHexa)
{
	unsigned int ret = 0, t = 0, n = 0;
	const char *c = pszHexa;

	while (*c && (n < 16)) {

		if ((*c >= '0') && (*c <= '9'))
			t = (*c - '0');
		else if((*c >= 'A') && (*c <= 'F'))
			t = (*c - 'A' + 10);
		else if((*c >= 'a') && (*c <= 'f'))
			t = (*c - 'a' + 10);
		else
			break;

		n++; ret *= 16; ret += t; c++;

		if (n >= 8)
			break;
	}

	return ret;
}

/*
 * check_ntpb_header - minimal check of the packet header
 */
int check_ntpb_header(void *buf)
{
	int i;
	unsigned char *pbuf = (unsigned char *)buf;

	for (i=0; i<ntpb_MagicSize; i++) {
		if (pbuf[i] != ntpb_hdrMagic[i])
			break;
	}

	if (i == ntpb_MagicSize)
		return 1;

	return 0;
}

/*
 * SendRemoteCmd - send a command to the remote server
 */
int SendRemoteCmd(int cmd, unsigned char *buf, int size)
{
	int ntpbpktSize, sndSize, rcvSize;

	memcpy(&pktbuffer[0], ntpb_hdrMagic, ntpb_MagicSize); /* copying NTPB Magic */
	*((unsigned short *)&pktbuffer[ntpb_MagicSize]) = size;
	*((unsigned short *)&pktbuffer[ntpb_MagicSize+2]) = cmd;

	if ((buf) && (size > 0)) {
		memcpy(&pktbuffer[ntpb_hdrSize], buf, size);
	}

	ntpbpktSize = ntpb_hdrSize + size;

	/* send the ntpb packet */
	sndSize = send(main_socket, (char *)&pktbuffer[0], ntpbpktSize, 0);
	if (sndSize <= 0)
		return -1;

	rcvSize = recv(main_socket, (char *)&pktbuffer[0], sizeof(pktbuffer), 0);
	if (rcvSize <= 0)
		return -2;

	/* packet sanity check */
	if (!check_ntpb_header(pktbuffer))
		return -3;

	/* reply check */
	if (*((unsigned short *)&pktbuffer[ntpb_hdrSize]) != 1)
		return -4;

	return 1;
}

/*
 * receiveData - retrieve datas sent by the server
 */
int receiveData(char *dumpfile, unsigned int dump_size, int flag)
{
	int rcvSize, sndSize, packetSize, ntpbpktSize, ntpbCmd, recv_size, sizeWritten;
	unsigned int dump_wpos = 0;
	int endTransmit = 0;
	FILE *fh_dump;

	if (flag) {
		/* create the dump file */
		fh_dump = fopen(dumpfile, "wb");
		if (!fh_dump)
			return -100;
	}

	while (1) {

		/* receive the first packet */
		rcvSize = recv(main_socket, (char *)&pktbuffer[0], sizeof(pktbuffer), 0);
		if (rcvSize < 0)
			return -1;

		/* packet sanity check */
		if (!check_ntpb_header(pktbuffer))
			return -2;

		ntpbpktSize = *((unsigned short *)&pktbuffer[6]);
		packetSize = ntpbpktSize + ntpb_hdrSize;

		recv_size = rcvSize;

		/* fragmented packet handling */
		while (recv_size < packetSize) {
			rcvSize = recv(main_socket, (char *)&pktbuffer[recv_size], sizeof(pktbuffer) - recv_size, 0);
			if (rcvSize < 0)
				return -1;
			else
				recv_size += rcvSize;
		}

		/* parses packet */
		if (check_ntpb_header(pktbuffer)) {
			ntpbCmd = *((unsigned short *)&pktbuffer[8]);

			switch(ntpbCmd) { /* treat Client Request here */

				case NTPBCMD_PRINT_EEDUMP:
				case NTPBCMD_PRINT_IOPDUMP:
				case NTPBCMD_PRINT_KERNELDUMP:
				case NTPBCMD_PRINT_SCRATCHPADDUMP:

					if (flag) {
						if ((dump_wpos + ntpbpktSize) > dump_size)
							return -3;

						sizeWritten = fwrite(&pktbuffer[ntpb_hdrSize], 1, ntpbpktSize, fh_dump);
						if (sizeWritten != ntpbpktSize)
							return -4;

						dump_wpos += sizeWritten;
					}
					break;

				case NTPBCMD_END_TRANSMIT:
					endTransmit = 1;
					break;
			}

			*((unsigned short *)&pktbuffer[ntpb_hdrSize]) = 1;
			*((unsigned short *)&pktbuffer[6]) = 0;
			packetSize = ntpb_hdrSize + 2;

			/* send the response packet */
			sndSize = send(main_socket, (char *)&pktbuffer[0], packetSize, 0);
			if (sndSize <= 0)
				return -5;

			/* catch end of dump transmission */
			if (endTransmit)
				break;
		}
	}

	if (flag)
		fclose(fh_dump);

	return 1;
}

/*
 * netlogThread - thread to log netlog messages to a text file
 */
void *netlogThread(void *thread_id)
{
	int udp_socket;
	struct sockaddr_in peer;
	int r;
	fd_set fd;
	FILE *fh_log;

	fh_log = fopen("netlog.log", "a");
	if (fh_log) {
		fclose(fh_log);
	}

	peer.sin_family = AF_INET;
	peer.sin_port = htons(SERVER_UDP_PORT);
	peer.sin_addr.s_addr = htonl(INADDR_ANY);

	udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if (udp_socket < 0)
		goto error;

  	if (bind(udp_socket, (struct sockaddr *)&peer, sizeof(struct sockaddr)) < 0)
		goto error;

	FD_ZERO(&fd);

	while (1) {
		FD_SET(udp_socket, &fd);

  		select(FD_SETSIZE, &fd, NULL, NULL, NULL);

		memset(netlogbuffer, 0, sizeof(netlogbuffer));

		r = recvfrom(udp_socket, (void*)netlogbuffer, sizeof(netlogbuffer), 0, NULL, NULL);

		fh_log = fopen("netlog.log", "a");
		if (fh_log) {
			fwrite(netlogbuffer, 1, r, fh_log);
			fclose(fh_log);
		}
	}

error:
	_closesocket(udp_socket);
	pthread_exit(thread_id);

	return 0;
}

/*
 * printLog - print log file to screen
 */
int printLog(void)
{
	FILE *fh_log;
	int logsize, r;
	char *buf;

	fh_log = fopen("netlog.log", "r");
	if (fh_log) {
		fseek(fh_log, 0, SEEK_END);
		logsize = ftell(fh_log);
		fseek(fh_log, 0, SEEK_SET);
		if (logsize) {
			buf = malloc(logsize);
			r = fread(buf, 1, logsize, fh_log);
			printf("%s\n", buf);
			free(buf);
		}
		fclose(fh_log);
	}

	return 0;
}

/*
 * clearLog - clear log file
 */
int clearLog(void)
{
	FILE *fh_log;

	fh_log = fopen("netlog.log", "w");
	if (fh_log) {
		fclose(fh_log);
	}

	return 0;
}

/*
 * execCmdHalt - send remote cmd HALT to server
 */
int execCmdHalt(void)
{
	int r;

	/* connect client */
	r = clientConnect();
	if (r < 0) {
		printf("failed to connect client! error: %d\n", r);
		return -1;
	}

	/* send remote cmd */
	r = SendRemoteCmd(REMOTE_CMD_HALT, NULL, 0);
	if (r < 0) {
		printf("failed to send remote command! error %d\n", r);
		return -4;
	}

	/* receive reply */
	r = receiveData(NULL, 0, 0);
	if (r < 0) {
		printf("failed to receive reply! error %d\n", r);
		return -5;
	}

	/* disconnect client */
	r = clientDisconnect();
	if (r < 0) {
		printf("failed to disconnect client! error: %d\n", r);
		return -2;
	}

	return 0;
}

/*
 * execCmdResume - send remote cmd RESUME to server
 */
int execCmdResume(void)
{
	int r;

	/* connect client */
	r = clientConnect();
	if (r < 0) {
		printf("failed to connect client! error: %d\n", r);
		return -1;
	}

	/* send remote cmd */
	r = SendRemoteCmd(REMOTE_CMD_RESUME, NULL, 0);
	if (r < 0) {
		printf("failed to send remote command! error %d\n", r);
		return -4;
	}

	/* receive reply */
	r = receiveData(NULL, 0, 0);
	if (r < 0) {
		printf("failed to receive reply! error %d\n", r);
		return -5;
	}

	/* disconnect client */
	r = clientDisconnect();
	if (r < 0) {
		printf("failed to disconnect client! error: %d\n", r);
		return -2;
	}

	return 0;
}

/*
 * execCmdDump - send remote cmd DUMP to server
 */
int execCmdDump(char *psz_memzone, char *psz_dumpstart, char *psz_dumpend, char *psz_outfile)
{
	int r, dump_size, remote_cmd;
	unsigned int dump_start, dump_end;
	unsigned char cmdBuf[16];

	/* just a few checks */
	if ((strcmp(psz_memzone, MEMZONE_EE)) && (strcmp(psz_memzone, MEMZONE_IOP)) && \
		(strcmp(psz_memzone, MEMZONE_KERNEL)) && (strcmp(psz_memzone, MEMZONE_SCRATCHPAD))) {
		printUsage();
		return -3;
	}

	dump_start = HexaToDecimal(psz_dumpstart);
	dump_end = HexaToDecimal(psz_dumpend);
	dump_size = (int)(dump_end - dump_start);

	if (dump_size <= 0) {
		printf("invalid address range...\n");
		return -3;
	}

	if (!strcmp(psz_memzone, MEMZONE_EE)) {
		if ((dump_start < 0x00080000) || (dump_start > 0x02000000) ||
			(dump_end < 0x0080000) || (dump_end > 0x02000000)) {
			printf("invalid address range for EE dump...\n");
			return -3;
		}
		remote_cmd = REMOTE_CMD_DUMPEE;
	}
	if (!strcmp(psz_memzone, MEMZONE_IOP)) {
		if ((dump_start < 0x00000000) || (dump_start > 0x00200000) ||
			(dump_end < 0x00000000) || (dump_end > 0x00200000)) {
			printf("invalid address range for IOP dump...\n");
			return -3;
		}
		remote_cmd = REMOTE_CMD_DUMPIOP;
	}
	if (!strcmp(psz_memzone, MEMZONE_KERNEL)) {
		if ((dump_start < 0x80000000) || (dump_start > 0x82000000) ||
			(dump_end < 0x80000000) || (dump_end > 0x82000000)) {
			printf("invalid address range for Kernel dump...\n");
			return -3;
		}
		remote_cmd = REMOTE_CMD_DUMPKERNEL;
	}
	if (!strcmp(psz_memzone, MEMZONE_SCRATCHPAD)) {
		if ((dump_start < 0x70000000) || (dump_start > 0x70004000) ||
			(dump_end < 0x70000000) || (dump_end > 0x70004000)) {
			printf("invalid address range for ScratchPad dump...\n");
			return -3;
		}
		remote_cmd = REMOTE_CMD_DUMPSCRATCHPAD;
	}

	/* connect client */
	r = clientConnect();
	if (r < 0) {
		printf("failed to connect client! error: %d\n", r);
		return -1;
	}

	/* fill remote cmd buffer */
	*((unsigned int *)&cmdBuf[0]) = dump_start;
	*((unsigned int *)&cmdBuf[4]) = dump_end;

	/* send remote cmd */
	r = SendRemoteCmd(remote_cmd, cmdBuf, 8);
	if (r < 0) {
		printf("failed to send remote command! error %d\n", r);
		return -4;
	}

	printf("Please wait while dumping %s @0x%08x-0x%08x to %s...\n", psz_memzone, dump_start, dump_end, psz_outfile);

	/* receive dump */
	r = receiveData(psz_outfile, dump_size, 1);
	if (r < 0) {
		printf("failed to receive dump datas! error %d\n", r);
		return -5;
	}

	printf("Dump done!\n");

	/* disconnect client */
	r = clientDisconnect();
	if (r < 0) {
		printf("failed to disconnect client! error: %d\n", r);
		return -2;
	}

	return 0;
}

/*
 * main function
 */
int main(int argc, char **argv, char **env)
{

	/* args check */
	if ((argc < 2) || (argc > 8)) {
		printUsage();
		return 0;
	}

#ifdef _WIN32
	/* Init WSA */
	WsaData = InitWS2();
	if (WsaData == NULL)
		return 0;
#endif

	/* Create netlog thread */
	pthread_create(&netlog_thread_id, NULL, netlogThread, (void *)&netlog_thread_id);

	/* parse args */
	if (argc == 2) {
		if (!strcmp(argv[1], "--halt") || !strcmp(argv[1], "-H"))
			execCmdHalt();
		else if (!strcmp(argv[1], "--resume") || !strcmp(argv[1], "-R"))
			execCmdResume();
		else if (!strcmp(argv[1], "--log") || !strcmp(argv[1], "--printlog") || !strcmp(argv[1], "-L"))
			printLog();
		else if (!strcmp(argv[1], "--clearlog") || !strcmp(argv[1], "-C"))
			clearLog();
		else if (!strcmp(argv[1], "--version") || !strcmp(argv[1], "-v"))
			printVer();
		else
			printUsage();
	}
	else if (argc == 4) {
		if (!strcmp(argv[1], "--halt") || !strcmp(argv[1], "-H")) {
			if (!strcmp(argv[2], "-ip")) {
				strcpy(g_server_ip, argv[3]);
				execCmdHalt();
			}
			else
				printUsage();
		}
		else if (!strcmp(argv[1], "--resume") || !strcmp(argv[1], "-R")) {
			if (!strcmp(argv[2], "-ip")) {
				strcpy(g_server_ip, argv[3]);
				execCmdResume();
			}
			else
				printUsage();
		}
		else
			printUsage();
	}
	else if (argc == 6) {
		if (!strcmp(argv[1], "--dump") || !strcmp(argv[1], "-D"))
			execCmdDump(argv[2], argv[3], argv[4], argv[5]);
		else
			printUsage();
	}
	else if (argc == 8) {
		if (!strcmp(argv[1], "--dump") || !strcmp(argv[1], "-D")) {
			if (!strcmp(argv[6], "-ip")) {
				strcpy(g_server_ip, argv[7]);
				execCmdDump(argv[2], argv[3], argv[4], argv[5]);
			}
			else
				printUsage();
		}
		else
			printUsage();
	}
	else
		printUsage();

#ifdef _WIN32
	WSACleanup();
#endif

	/* End program. */
	exit(EXIT_SUCCESS);

	return 0;
}