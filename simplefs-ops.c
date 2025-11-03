#include "simplefs-ops.h"
extern struct filehandle_t file_handle_array[MAX_OPEN_FILES]; // Array for storing opened files

// ----------------- Helper Functions -----------------

static int find_inode_by_name(char *filename) {
    struct inode_t inode;
    for (int i = 0; i < NUM_INODES; i++) {
        simplefs_readInode(i, &inode);
        if (inode.status == INODE_IN_USE && strcmp(inode.name, filename) == 0)
            return i;
    }
    return -1;
}

static int find_free_filehandle() {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (file_handle_array[i].inode_number == -1)
            return i;
    }
    return -1;
}

int simplefs_create(char *filename){
    /*
	    Create file with name `filename` from disk
	*/
	// Check if file already exists
    if (find_inode_by_name(filename) != -1)
        return -1;

    // Allocate inode
    int inodenum = simplefs_allocInode();
    if (inodenum == -1)
        return -1;

    // Initialize inode
    struct inode_t inode;
    inode.status = INODE_IN_USE;
    strncpy(inode.name, filename, MAX_NAME_STRLEN);
    inode.name[MAX_NAME_STRLEN - 1] = '\0';
    inode.file_size = 0;
    for (int i = 0; i < MAX_FILE_SIZE; i++)
        inode.direct_blocks[i] = -1;

    simplefs_writeInode(inodenum, &inode);
    return inodenum;
}


void simplefs_delete(char *filename){
    /*
	    delete file with name `filename` from disk
	*/
    int inodenum = find_inode_by_name(filename);
    if (inodenum == -1)
        return;

    struct inode_t inode;
    simplefs_readInode(inodenum, &inode);

    // Free allocated data blocks
    for (int i = 0; i < MAX_FILE_SIZE; i++) {
        if (inode.direct_blocks[i] != -1) {
            simplefs_freeDataBlock(inode.direct_blocks[i]);
            inode.direct_blocks[i] = -1;
        }
    }

    simplefs_freeInode(inodenum);
}

int simplefs_open(char *filename){
    /*
	    open file with name `filename`
	*/
    int inodenum = find_inode_by_name(filename);
    if (inodenum == -1)
        return -1;

    int handle = find_free_filehandle();
    if (handle == -1)
        return -1;

    file_handle_array[handle].inode_number = inodenum;
    file_handle_array[handle].offset = 0;
    return handle;
}

void simplefs_close(int file_handle){
    /*
	    close file pointed by `file_handle`
	*/
    if (file_handle < 0 || file_handle >= MAX_OPEN_FILES)
        return;
    file_handle_array[file_handle].inode_number = -1;
    file_handle_array[file_handle].offset = 0;
}

int simplefs_read(int file_handle, char *buf, int nbytes){
    /*
	    read `nbytes` of data into `buf` from file pointed by `file_handle` starting at current offset
	*/
    if (file_handle < 0 || file_handle >= MAX_OPEN_FILES)
        return -1;

    int inodenum = file_handle_array[file_handle].inode_number;
    if (inodenum == -1)
        return -1;

    struct inode_t inode;
    simplefs_readInode(inodenum, &inode);

    int offset = file_handle_array[file_handle].offset;
    if (offset + nbytes > inode.file_size)
        return -1; // cannot read beyond EOF

    int bytes_read = 0;
    int current_offset = offset;

    while (bytes_read < nbytes) {
        int block_index = current_offset / BLOCKSIZE;
        int block_offset = current_offset % BLOCKSIZE;
        int bytes_to_copy = BLOCKSIZE - block_offset;
        if (bytes_to_copy > nbytes - bytes_read)
            bytes_to_copy = nbytes - bytes_read;

        if (block_index >= MAX_FILE_SIZE || inode.direct_blocks[block_index] == -1)
            return -1;

        char block_buf[BLOCKSIZE];
        simplefs_readDataBlock(inode.direct_blocks[block_index], block_buf);

        memcpy(buf + bytes_read, block_buf + block_offset, bytes_to_copy);

        bytes_read += bytes_to_copy;
        current_offset += bytes_to_copy;
    }

    return 0;
}


int simplefs_write(int file_handle, char *buf, int nbytes){
    /*
	    write `nbytes` of data from `buf` to file pointed by `file_handle` starting at current offset
	*/
    if (file_handle < 0 || file_handle >= MAX_OPEN_FILES)
        return -1;

    int inodenum = file_handle_array[file_handle].inode_number;
    if (inodenum == -1)
        return -1;

    struct inode_t inode;
    simplefs_readInode(inodenum, &inode);

    int offset = file_handle_array[file_handle].offset;
    if (offset + nbytes > MAX_FILE_SIZE * BLOCKSIZE)
        return -1; // exceeds max file size

    // Track newly allocated blocks to free on rollback
    int newly_allocated[MAX_FILE_SIZE] = {0};

    int bytes_written = 0;
    int current_offset = offset;

    while (bytes_written < nbytes) {
        int block_index = current_offset / BLOCKSIZE;
        int block_offset = current_offset % BLOCKSIZE;
        int bytes_to_copy = BLOCKSIZE - block_offset;
        if (bytes_to_copy > nbytes - bytes_written)
            bytes_to_copy = nbytes - bytes_written;

        if (block_index >= MAX_FILE_SIZE)
            goto fail;

        int blocknum = inode.direct_blocks[block_index];
        char block_buf[BLOCKSIZE];
        if (blocknum == -1) {
            blocknum = simplefs_allocDataBlock();
            if (blocknum == -1)
                goto fail;
            inode.direct_blocks[block_index] = blocknum;
            newly_allocated[block_index] = 1;
            memset(block_buf, 0, BLOCKSIZE);
        } else {
            simplefs_readDataBlock(blocknum, block_buf);
        }

        memcpy(block_buf + block_offset, buf + bytes_written, bytes_to_copy);
        simplefs_writeDataBlock(blocknum, block_buf);

        bytes_written += bytes_to_copy;
        current_offset += bytes_to_copy;
    }

    if (offset + nbytes > inode.file_size)
        inode.file_size = offset + nbytes;

    simplefs_writeInode(inodenum, &inode);
    return 0;

fail:
    // rollback newly allocated blocks
    for (int i = 0; i < MAX_FILE_SIZE; i++) {
        if (newly_allocated[i]) {
            simplefs_freeDataBlock(inode.direct_blocks[i]);
            inode.direct_blocks[i] = -1;
        }
    }
    simplefs_writeInode(inodenum, &inode);
    return -1;
}


int simplefs_seek(int file_handle, int nseek){
    /*
	   increase `file_handle` offset by `nseek`
	*/
    if (file_handle < 0 || file_handle >= MAX_OPEN_FILES)
        return -1;

    int inodenum = file_handle_array[file_handle].inode_number;
    if (inodenum == -1)
        return -1;

    struct inode_t inode;
    simplefs_readInode(inodenum, &inode);

    int new_offset = file_handle_array[file_handle].offset + nseek;
    if (new_offset < 0 || new_offset > inode.file_size)
        return -1;

    file_handle_array[file_handle].offset = new_offset;
    return 0;
}