#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "udp.h"
// #include "message.h"
#include "ufs.h"
# include "mfs.h"

//globally declared socket descriptor
int sd;
int fd;

typedef struct {
	dir_ent_t entries[128];
} dir_block_t;

void intHandler(int dummy) {
    UDP_Close(sd);
    exit(130);
}

typedef struct {
	unsigned int bits[UFS_BLOCK_SIZE / sizeof(unsigned int)];
} bitmap_t;


//global structures 
inode_t *inode_table;
bitmap_t *inode_bit_map;
bitmap_t *data_bit_map;
void *image;
super_t *s;

void clear_bit (bitmap_t *bmap, int map_num) {
    int index = map_num / 32;
    bmap->bits[index] = bmap->bits[index] & ~ (1 << (31-map_num));
}

int is_BitMap_Set (bitmap_t *bmap, int map_num) {
    int index = map_num / 32;
    int num = bmap->bits[index];
    if ( num & (1 << (31-map_num) ) ) 
        return 1;
    else 
        return 0;
}

int find_free_and_set_bitmap ( bitmap_t *bmap) {
       // find the inode free
    int free_inode_num ;
    int found  = 0;
    for ( int i  = 0 ; i < 1024 ; i++) {
        unsigned int value = bmap->bits[i] ;
        for (int j = 31; j >= 0; j--) {
            if (!(value & (1 << j)) && !found) {
                free_inode_num = (32*i)+(31-j);
                found = 1;
                // make inode bitmap for the entry as 1 
                bmap->bits[i] = bmap->bits[i] | (1 << j);
                break;
            }
        }
        if (found)
            break;
    }
    if (!found) {
        return -1;
    }
    return free_inode_num;
}

int server_stat(int inum, MFS_Stat_t *stat) {
    if (is_BitMap_Set (inode_bit_map, inum) == 0) {
        return -1;
    }

    inode_t *inode = inode_table + inum;
    // printf("stat size: %d\n", inode->size);
    stat->size = inode->size;
    stat->type = inode->type;
    return 0;
}

int server_unlink (int pinum , char* name ) {
    if ( is_BitMap_Set (inode_bit_map, pinum) == 0 ) {
        perror("bit not set\n");
        return -1;
    }

    inode_t *inode = inode_table + pinum;

    if (inode->type != 0 ) {
        perror("Type is not directory\n");
        return -1;        
    }

    int rm_inode ;
    int found = 0;
    dir_ent_t *directory;
    int entry = 0;
    for ( int i =0 ; i < DIRECT_PTRS ; i++) {
        if (inode->direct[i] != -1 ) {
            directory = image + (inode->direct[i] * UFS_BLOCK_SIZE); 
            for (int j = 0 ; j < 128 ; j++) {
                if(strncmp(directory[j].name, name, 28) == 0) {
                    //strcpy ( name , directory[i].name );  
                    rm_inode =  directory[j].inum;
                    // remove the directory entry
                    entry = j;
                    found = 1;  
                    break;           
                }
            }
        }
    }

    inode_t *tar_inode;
    int rm_size  ;
    if (found) {
        tar_inode = inode_table + rm_inode;
        if (tar_inode->type == 0 ) {
            // directory not empty
            if (tar_inode ->size != (2 * sizeof(dir_ent_t))) 
                return -1;
        }
        rm_size = tar_inode->size;
        clear_bit (inode_bit_map, rm_inode);
    }

    if (found) {
        directory[entry].inum = -1;
    }

    //check the data block linked to the inode and reset the data bitmap
    for ( int i =0 ; i < DIRECT_PTRS ; i++) {
        if (tar_inode->direct[i] != -1 ) {
            int dnum = tar_inode->direct[i];
            clear_bit (data_bit_map, dnum-(s->data_region_addr));
        }
    }

    if ( tar_inode->type == 1) {
        inode->size -= sizeof(dir_ent_t);
    }

    inode->size -= rm_size;

    return 0;
}

int server_lookup ( int inum, char *name) {

    if ( is_BitMap_Set (inode_bit_map, inum) == 0 ) {
        return -1;
    }

    inode_t *inode = inode_table + inum ;
    //if its a file
    if ( inode->type == 1) {
        return -1;
    }
    for ( int i =0 ; i < DIRECT_PTRS ; i++) {
        if (inode->direct[i] != -1 ) {
            dir_ent_t *directory = image + (inode->direct[i] * UFS_BLOCK_SIZE); 
            for (int j = 0 ; j < 128 ; j++) {
                if(strncmp(directory[j].name, name, NAME_LENGTH) == 0) {
                    //strcpy ( name , directory[i].name );                
                    return directory[j].inum;     
                }
            }
        }
    }

    //if not found
    return -1;
}

int server_read ( int inum, char *buffer, int offset, int nbytes ) { 


    if ( is_BitMap_Set (inode_bit_map, inum) == 0 ) {
        return -1;
    }

    inode_t *inode = inode_table + inum ;

    int dm_index = (offset / 4096);

    if (dm_index > 29) 
        return -1;

    int page_offset = offset % 4096;

    // for the data file
    char *first_block_addr = image + (inode->direct[dm_index] * UFS_BLOCK_SIZE);    
    char *start_addr = first_block_addr + page_offset ;

    if ( page_offset + nbytes > 4096) {
        int available = 4096-page_offset;
        memcpy(buffer, start_addr, available);   
        int overflow =   (page_offset + nbytes) - 4096; 
        if (inode ->direct[dm_index+1] == -1 ) {
            return -1;
        }
        start_addr = image + ( inode->direct[dm_index+1] * UFS_BLOCK_SIZE);
        memcpy(buffer+available, start_addr, overflow);   
    }
    else { 
        //read from the page for the offset , offset+nbytes space
        memcpy(buffer, start_addr, nbytes);
    }
    
    return 0;
}


int server_write ( int inum, char *buffer, int offset, int nbytes ) {
    if ( is_BitMap_Set (inode_bit_map, inum) == 0 ) {
        return -1;
    }

    inode_t *inode = inode_table + inum ;
    if ( inode -> type != UFS_REGULAR_FILE) 
        return -1;
    
    int dm_index = (offset / 4096);

    if (dm_index > 29) 
        return -1;
    int page_offset = offset % 4096;

    if ( inode ->direct[dm_index] == -1) {
        // get a free data block and link and then go to the block and write
        int free_dnode_num = find_free_and_set_bitmap (data_bit_map);
        if ( free_dnode_num == -1 ) 
            return -1;
        inode ->direct[dm_index] = s->data_region_addr + free_dnode_num;
    }

    //spans across 2 pages
    int overflow = 0;
    char *first_block_addr = image + (inode->direct[0] * UFS_BLOCK_SIZE);
    char *start_addr = first_block_addr + offset ;

    if ( page_offset + nbytes > 4096) {
        //write to first page for the available space
        int available = 4096-page_offset;
        memcpy(start_addr, buffer, available);

        overflow =   (page_offset + nbytes) - 4096;
        if (inode ->direct[dm_index+1] == -1 ) { 
            int free_dnode_num = find_free_and_set_bitmap (data_bit_map);
            if ( free_dnode_num == -1 ) 
                return -1;
            inode->direct[dm_index+1] = s->data_region_addr + free_dnode_num;
        }

        start_addr = image + ( inode->direct[dm_index+1] * UFS_BLOCK_SIZE);
        //write to second page for the 0 , overflow space       
        memcpy(start_addr, buffer+available, overflow);                        
    }
    else { 
        //write to the page for the offset , offset+nbytes space
        memcpy(start_addr, buffer, nbytes);
    }
    inode -> size = ( offset + nbytes  <= inode -> size ) ? inode -> size : offset + nbytes;    return 0;
}


int server_create ( int create_pinum , int create_type , char *create_name) {
    // get the address of the parent_inode
    inode_t *parent_inode = inode_table + create_pinum;
    dir_ent_t *directory = image + (parent_inode->direct[0] * UFS_BLOCK_SIZE);

    if ( is_BitMap_Set (inode_bit_map, create_pinum) == 0 ) 
        return -1;
 
    if (parent_inode->type == 1 ) {
        perror("return error because the parent should be a dir"); 
        return -1;      
    }
    else {
        for ( int i = 0 ; i < 128 ; i++ ) {
            if ( strcmp(directory[i].name,create_name) == 0 ) {
                perror("return success because file/repo already exist in the repo");
                return 0;                   
            }
        } 
    }

    // find the inode free
    int free_inode_num ;
    free_inode_num = find_free_and_set_bitmap (inode_bit_map);

    if ( free_inode_num == -1) 
        return -1;

    inode_t *free_inode = inode_table + free_inode_num;
    free_inode-> type = create_type;
    free_inode-> size = 0;

    //find the datamap free
    int map_start = 0;
    if ( parent_inode->type == 0 ) {
        int free_dnode_num = find_free_and_set_bitmap (data_bit_map);
        if ( free_dnode_num == -1 ) 
            return -1;
        //added link from inode to the data block
        free_inode->direct[0] = s->data_region_addr + free_dnode_num;
        map_start ++;
    }

    for (int i = map_start ; i < DIRECT_PTRS; i++)
	    free_inode->direct[i] = -1;

    //add the entry to the parent directory 
    if ( parent_inode->type == 0 ) {
        for ( int i = 0 ; i < 128 ; i++ ) {
            if ( directory[i].inum == -1 ) {
                directory[i].inum = free_inode_num;
                strcpy(directory[i].name, create_name);
                break;
            }
        }
    }

    //if its directory should add current and parent iunum to the directory
    if (create_type == 0) {

        dir_block_t *new_directory =  image + (free_inode->direct[0] * UFS_BLOCK_SIZE);
        parent_inode->size += 2 * sizeof(dir_ent_t);

        memcpy(new_directory->entries[0].name, ".",strlen("."));
        new_directory->entries[0].inum = free_inode_num ;

        memcpy(new_directory->entries[1].name, "..",strlen(".."));
        new_directory->entries[1].inum = create_pinum;
    
        free_inode-> size = 2 * sizeof(dir_ent_t);

        for (int i = 2; i < 128; i++)
	        new_directory->entries[i].inum = -1;
    }
    else {
        parent_inode->size += sizeof(dir_ent_t);        
    }

    return 0;
}

int server_shutdown() {
    fsync(fd);
    close(fd);
    UDP_Close(sd);
    exit(0);
}


int main(int argc, char *argv[]) {
    signal(SIGINT, intHandler);
    if ( argc < 3) {
        perror("Error : Server arguments less than 2 ");
    }

    int port = atoi(argv[1]);
    char *img_file = argv[2];

    sd = UDP_Open(port);
    assert(sd > -1);   
    fd = open(img_file, O_RDWR);
    assert(fd > -1);

    struct stat sbuf;
    int rc = fstat(fd, &sbuf);
    assert(rc > -1);

    int image_size = (int) sbuf.st_size;
    image = mmap(NULL, image_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    assert(image != MAP_FAILED);

    s = (super_t *) image;

    inode_table = image + (s->inode_region_addr * UFS_BLOCK_SIZE);
    // inode_t *root_inode = inode_table;


    // dir_ent_t *root_dir = image + (root_inode->direct[0] * UFS_BLOCK_SIZE);

    //Message 
    // message_t m;
    // m.mtype = 0 ;

    inode_bit_map = image + (s->inode_bitmap_addr*UFS_BLOCK_SIZE);
    data_bit_map = image + (s->data_bitmap_addr*UFS_BLOCK_SIZE);

    while (1) {
        struct sockaddr_in addr;
        message msg, response;
        printf("server:: waiting...\n");
        int rc = UDP_Read(sd, &addr, (char*) &msg, sizeof(message));
        if (rc > 0) {
            printf("server:: read message [size:%d, inum:%d, content:(%s)]\n", rc, msg.inum, msg.data);
            response.errCode = 0;
            response.requestType = RESPONSE;
            // handle()
            if (msg.requestType == WRITE) {
                int return_code = server_write(msg.inum, msg.data, msg.offset, msg.nbytes);
                fsync(fd);
                if(return_code != 0) response.errCode=-1;
            } 
            else if (msg.requestType == READ) {

                int return_code = server_read(msg.inum, response.data, msg.offset, msg.nbytes);
                if(return_code != 0) response.errCode=-1;

            } 
            else if (msg.requestType == STAT) {

                int return_code = server_stat(msg.inum, &response.stat);
                if(return_code != 0) response.errCode=-1;
            } 
            else if (msg.requestType == CREAT) {

                int return_code = server_create(msg.inum, msg.type, msg.name);
                fsync(fd);
                if(return_code != 0) response.errCode=-1;
            }
            else if (msg.requestType == LOOKUP) {
                // lookup
                int retInode = server_lookup(msg.inum, msg.name);
                response.inum = retInode;
            }
            else if (msg.requestType == UNLINK) {
                int retCode = server_unlink(msg.inum, msg.name);
                fsync(fd);
                if( retCode != 0 ) response.errCode=-1;
                // unlink
            }
            else if (msg.requestType == SHUTDOWN) {
                // shutdown
                fsync(fd);
                server_shutdown();
            }

            rc = UDP_Write(sd, &addr, (char*) &response, sizeof(message));
        } 
    }
    return 0; 

}