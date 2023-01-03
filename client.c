#include <stdio.h>
#include "udp.h"
#include "mfs.c"

#define BUFFER_SIZE (1000)

int main(int argc, char *argv[]) {

    //init
    char* hostname = "localhost";
    int ret = MFS_Init(hostname, 10000);


    // create
    ret = MFS_Creat(0, 0, "myfolder");
    printf("create dir status: %d\n", ret);

    ret = MFS_Creat(0, 1, "file.txt");
    printf("create file status: %d\n", ret);

    //write
    char buffer[MFS_BLOCK_SIZE];
    sprintf(buffer, "hello from the other side");
    printf("buffer: %s\n", buffer);
    ret = MFS_Write(2, buffer, 0,  21);
    printf("write to file status: %d\n", ret);

    ret = MFS_Write(1, buffer, 0,  21);
    printf("write to dir status: %d\n", ret);

    //read
    char readBuffer[MFS_BLOCK_SIZE];
    ret = MFS_Read(2, readBuffer, 2,  21);
    printf("read success: %d\n", ret);
    printf("mfs read: %s\n", readBuffer);

    ret = MFS_Read(1, readBuffer, 0,  10);
    printf("read failure: %d\n", ret);
    printf("mfs read: %s\n", readBuffer);

    // lookup
    int inum = MFS_Lookup(0, "myfolder");
    printf("lookup inum: %d\n", inum);
    
    // lookup
    inum = MFS_Lookup(2, "myfolder");
    printf("lookup in file return val: %d\n", inum);

    // stat
    MFS_Stat_t m;
    ret = MFS_Stat(1, &m);
    if (ret == 0) printf("mfs size: %d\n", m.size);
    
    // create
    ret = MFS_Creat(1, 0, "myfoldermyfoldermyfoldermyfoldermyfoldermyfoldermyfolder");
    printf("create failure: %d\n", ret);
    
    return 0;
}


// client code
// int main(int argc, char *argv[]) {
//     struct sockaddr_in addrSnd, addrRcv;

//     int sd = UDP_Open(20000);
//     int rc = UDP_FillSockAddr(&addrSnd, "localhost", 10000);

//     char message[BUFFER_SIZE];
//     sprintf(message, "hello world");

//     printf("client:: send message [%s]\n", message);
//     rc = UDP_Write(sd, &addrSnd, message, BUFFER_SIZE);
//     if (rc < 0) {
// 	printf("client:: failed to send\n");
// 	exit(1);
//     }

//     printf("client:: wait for reply...\n");
//     rc = UDP_Read(sd, &addrRcv, message, BUFFER_SIZE);
//     printf("client:: got reply [size:%d contents:(%s)\n", rc, message);
//     return 0;
// }




