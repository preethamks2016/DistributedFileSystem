#include<stdio.h>
#include "udp.h"
#include "mfs.h"
#include <time.h>

// client socket
int sd;

int clientPort = 21000; 

//server socket
struct sockaddr_in serverAddr, addr;

int callServer(message *sendMsg, message *responseMsg) {
    printf("calling UDP write\n");
    int rc = UDP_Write(sd, &serverAddr, (char*) sendMsg, sizeof(message));
    if (rc < 0) {
        printf("client:: failed UDP_Write request\n");
        return -1;
    }

    // wait for response / timeout
    fd_set readfds;
    struct timeval timeVal;
    timeVal.tv_sec=3;
    timeVal.tv_usec=0;

    FD_ZERO(&readfds);
    FD_SET(sd, &readfds);
    if(select(sd+1, &readfds, NULL, NULL, &timeVal)) {
        // read response
        rc = UDP_Read(sd, &addr, (char*) responseMsg, sizeof(message));
        if (rc < 0) {
            printf("client:: failed UDP_Read request\n");
            return -1;
        }
    } else {
        return -1;
    }

    if (responseMsg->errCode != 0 ) return -1;
    else return 0;
}

int MFS_Init(char *hostname, int port)
{
    if (hostname == NULL || port<0)
        return -1;

    // create client socket
    sd = UDP_Open(clientPort);

    // bind server socket address
    int rc = UDP_FillSockAddr(&serverAddr, hostname, port);
    if (rc != 0) {
        printf("error binding server socket\n");
        return -1;
    }
    return 0;
}

int MFS_Lookup(int pinum, char *name) {
    //checks
    if(strlen(name) > NAME_LENGTH || name == NULL || pinum < 0) return -1;
    message sentMsg, responseMsg;
    sentMsg.requestType = LOOKUP;
    sentMsg.inum = pinum;
    strcpy(sentMsg.name, name);
    int ret = callServer(&sentMsg, &responseMsg);
    if(ret != 0) return -1;
    return responseMsg.inum;
}

int MFS_Stat(int inum, MFS_Stat_t *m) {
    //checks
    if (inum < 0) return -1;
    message sentMsg, responseMsg;
    sentMsg.requestType = STAT;
    sentMsg.inum = inum;
    int ret = callServer(&sentMsg, &responseMsg);
    if(ret != 0) return -1;
    m->size = responseMsg.stat.size;
    m->type = responseMsg.stat.type;
    return 0;
}

int MFS_Write(int inum, char *buffer, int offset, int nbytes) {
    // checks
    if (nbytes == 0) return 0;
    if (inum < 0 || offset < 0 || nbytes < 0 || nbytes > MFS_BLOCK_SIZE) return -1;
    
    message sentMsg;
    message responseMsg;
    sentMsg.requestType = WRITE;
    sentMsg.inum = inum;
    sentMsg.offset = offset;
    sentMsg.nbytes = nbytes;
    memcpy(sentMsg.data, buffer, nbytes);
    printf("Adding to check\n");
    int ret = callServer(&sentMsg, &responseMsg);
    if(ret != 0) return -1;
    return ret;
}

int MFS_Read(int inum, char *buffer, int offset, int nbytes) {
    // checks
    if (nbytes == 0) return 0;
    if (inum < 0 || !buffer || offset < 0 || nbytes < 0 || nbytes > MFS_BLOCK_SIZE) return -1;

    message sentMsg, responseMsg;
    sentMsg.requestType = READ;
    sentMsg.inum = inum;
    sentMsg.offset = offset;
    sentMsg.nbytes = nbytes;
    int ret = callServer(&sentMsg, &responseMsg);
    if(ret != 0) return -1;
    memcpy(buffer, responseMsg.data, nbytes);
    return 0;
}

int MFS_Creat(int pinum, int type, char *name) {
	//	checks
	if(strlen(name) > NAME_LENGTH || name == NULL || pinum < 0 || type > 1 || type < 0) return -1;
    message sentMsg, responseMsg;
    sentMsg.requestType = CREAT;
    sentMsg.inum = pinum;
    sentMsg.type = type;
    strcpy(sentMsg.name, name);
    int ret = callServer(&sentMsg, &responseMsg);
	if(ret != 0) return -1;
    return 0;
}

int MFS_Unlink(int pinum, char *name) {
	//	checks
	if(strlen(name) > NAME_LENGTH || name == NULL || pinum < 0) return -1;
    message sentMsg, responseMsg;
    sentMsg.requestType = UNLINK;
    sentMsg.inum = pinum;
    strcpy(sentMsg.name, name);
    int ret = callServer(&sentMsg, &responseMsg);
	if(ret != 0) return -1;
    return 0;
}

int MFS_Shutdown() {
    message sentMsg, responseMsg;
    sentMsg.requestType = SHUTDOWN;
    callServer(&sentMsg, &responseMsg);
    UDP_Close(sd);
    return 0;
}

