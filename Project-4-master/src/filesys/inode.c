#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define INDIRECT_BLOCK 1        /* Number of indirect blocks in an inode */
#define DOUBLE_INDIRECT_BLOCK 1 /* Number of double indirect blocks
                                          in an inode */
#define INDIRECT_BLOCK_POINTERS 128  /* Number of pointers in an
                                          indirect block */
#define DOUBLE_INDIRECT_BLOCK_POINTERS 16384 /* Number of total pointers in a
                                                        double indirect block */

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}


/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{

    ASSERT (inode != NULL);

    int temp_position;
    int index;
    if (pos < inode->length){


    /* If offset is located in the direct block sector, calculate index
      and return the block sector wanted */
    /* Driven by Miranda */
    if (pos < DIRECT_BLOCK*BLOCK_SECTOR_SIZE){
      index = pos/BLOCK_SECTOR_SIZE;
      return inode->direct_blocks[index];
    }


    /* If offset is located in the indirect block sector, access the sector
       and return the block sector wanted */
    else if (pos <
              ((DIRECT_BLOCK + INDIRECT_BLOCK_POINTERS ) * BLOCK_SECTOR_SIZE)){
      block_sector_t indirect_pointers[INDIRECT_BLOCK_POINTERS];

      /* Get sector from Pointer */
      block_read (fs_device, inode->indirect_block, &indirect_pointers);

      /* Remove pos size from the Direct Blocks */
      temp_position = pos - (DIRECT_BLOCK*BLOCK_SECTOR_SIZE);

      index = temp_position/BLOCK_SECTOR_SIZE;
      return indirect_pointers[index];
    }

    /* Double indirect blocks */
    else if (pos <
      ((DIRECT_BLOCK + INDIRECT_BLOCK_POINTERS + DOUBLE_INDIRECT_BLOCK_POINTERS)
                       * BLOCK_SECTOR_SIZE)){
      /* Gets first sector*/
      block_sector_t double_indirect_pointers[INDIRECT_BLOCK_POINTERS];

      /* Gets sector for first IB */
      block_read (fs_device, inode->double_indirect_block,
                                                &double_indirect_pointers);


      block_sector_t double_indirect_pointers2[INDIRECT_BLOCK_POINTERS];

      /* Removes the pos size from the direct blocks + indirect  */
      temp_position = pos - ( (DIRECT_BLOCK + INDIRECT_BLOCK_POINTERS)
                                                  * BLOCK_SECTOR_SIZE );

      index = temp_position/(INDIRECT_BLOCK_POINTERS * BLOCK_SECTOR_SIZE);

      /* Gets sector for first IB */
      block_read (fs_device, double_indirect_pointers[index],
                                                &double_indirect_pointers2);

      index = temp_position/BLOCK_SECTOR_SIZE;
      return double_indirect_pointers2[index];
    }
  }
  return -1;
 }

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
   /* Driven by Gage */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;
  ASSERT (length >= 0);
  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->is_dir = is_dir;
      if(inode_allocate(disk_inode)) {
        block_write (fs_device, sector, disk_inode);
        success = true;
      }
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;
  /* Initialize. */
  lock_init(&inode->lock);
  list_push_front (&open_inodes, &inode->elem);
  lock_acquire(&inode->lock);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;

  block_read (fs_device, inode->sector, &inode->data);
  inode->length = inode->data.length;
  inode->is_dir = inode->data.is_dir;
  inode->indirect_block = inode->data.indirect_block;
  inode->double_indirect_block = inode->data.double_indirect_block;

  memcpy(&inode->direct_blocks, &inode->data.direct_blocks,
                                         DIRECT_BLOCK*sizeof(block_sector_t));
  lock_release(&inode->lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  lock_acquire(&inode->lock);
  if (inode != NULL)
    inode->open_cnt++;
  lock_release(&inode->lock);
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk. (Does it?  Check code.)
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  struct inode_disk inode_disk;
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  lock_acquire(&inode->lock);
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          free_map_release (inode->sector, 1);
          inode_clear(inode);
        }
        else{
          inode_disk.length = inode->length;
          inode_disk.is_dir = inode->is_dir;
          inode_disk.magic = INODE_MAGIC;
          memcpy(&inode_disk.direct_blocks, &inode->direct_blocks,
                                       DIRECT_BLOCK * sizeof(block_sector_t));
          inode_disk.indirect_block = inode->indirect_block;
          inode_disk.double_indirect_block = inode->double_indirect_block;
          block_write(fs_device, inode->sector, &inode_disk);
        }
      lock_release(&inode->lock);
      free (inode);
    }
    else
      lock_release(&inode->lock);
 }

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  lock_acquire(&inode->lock);
  inode->removed = true;
  lock_release(&inode->lock);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;
  /* extend at end of file */

  if(offset + size > inode->length) {
    if(inode_grow(inode, size + offset)) {
      lock_acquire(&inode->lock);
      inode->length = size + offset;
      inode->data.length = size + offset;
      lock_release(&inode->lock);
    }
  }

    while (size > 0)
      {
        /* Sector to write, starting byte offset within sector. */
        block_sector_t sector_idx = byte_to_sector (inode, offset);
        int sector_ofs = offset % BLOCK_SECTOR_SIZE;

        /* Bytes left in inode, bytes left in sector, lesser of the two. */
        off_t inode_left = inode->length - offset;
        int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
        int min_left = inode_left < sector_left ? inode_left : sector_left;

        /* Number of bytes to actually write into this sector. */
        int chunk_size = size < min_left ? size : min_left;
        if (chunk_size <= 0) {
          break;
        }

        if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
         {
          /* Write full sector directly to disk. */
          block_write (fs_device, sector_idx, buffer + bytes_written);
         }
       else
         {
          /* We need a bounce buffer. */
           if (bounce == NULL)
             {
               bounce = malloc (BLOCK_SECTOR_SIZE);
               if (bounce == NULL)
                 break;
             }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
           if (sector_ofs > 0 || chunk_size < sector_left){
             block_read (fs_device, sector_idx, bounce);
           }
           else
             memset (bounce, 0, BLOCK_SECTOR_SIZE);
           memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
           block_write (fs_device, sector_idx, bounce);
         }

        /* Advance. */
        size -= chunk_size;
        offset += chunk_size;
        bytes_written += chunk_size;

      }
    free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{

  lock_acquire(&inode->lock);
  inode->deny_write_cnt++;
  lock_release(&inode->lock);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  lock_acquire(&inode->lock);
  inode->deny_write_cnt--;
  lock_release(&inode->lock);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}


/* Go through whole shebang in office hours */
bool
inode_grow (struct inode *inode, off_t length) {
  lock_acquire(&inode->lock);
  char zero_out[512];
  int i;
  for (i=0;i<512;i++){
    zero_out[i]=0;
  }
  block_sector_t indirect_pointers[INDIRECT_BLOCK_POINTERS];
  int sectors = bytes_to_sectors(length) - bytes_to_sectors(inode->length);
  if(sectors==0){
    lock_release(&inode->lock);
    return 1;
  }
  int end_of_file = bytes_to_sectors(inode->length);
  /* Starting Index */
  int direct_block_number = end_of_file;
  /* For direct blocks */
  while(sectors != 0 && direct_block_number < DIRECT_BLOCK) {
    bool temp = free_map_allocate(1,&inode->direct_blocks[direct_block_number]);
    if(!temp) {
      lock_release(&inode->lock);
      return 0;
    }
    block_write(fs_device, inode->direct_blocks[direct_block_number], zero_out);
    direct_block_number++;
    sectors--;
  }
  end_of_file -= DIRECT_BLOCK;
  /* For indirect blocks */
  if(sectors != 0 && end_of_file < (INDIRECT_BLOCK_POINTERS)) {
    int indirect_block_number = end_of_file;
    /* Checks if it has indirect */
    block_sector_t indirect_pointers[INDIRECT_BLOCK_POINTERS];
    if(inode->indirect_block == NULL) {
      bool temp = free_map_allocate(1, &inode->indirect_block);
      if(!temp) {
        return 0;
      }
      block_write(fs_device, inode->indirect_block, zero_out);
      indirect_block_number = 0;
    }
    else
      block_read(fs_device, inode->indirect_block, &indirect_pointers);

    /* Loops through indirect array, allocates direct blocks */
    while(sectors != 0 && indirect_block_number < INDIRECT_BLOCK_POINTERS) {
      bool temp = free_map_allocate(1,
                                     &indirect_pointers[indirect_block_number]);
      if(!temp) {
        lock_release(&inode->lock);
        return 0;
      }
      block_write(fs_device, indirect_pointers[indirect_block_number],
                                                                     zero_out);
      indirect_block_number++;
      sectors--;
    }
    block_write(fs_device, inode->indirect_block, &indirect_pointers);
  }
  end_of_file -= INDIRECT_BLOCK_POINTERS;

  /* Double Indirect Blocks */
  if(sectors != 0 && end_of_file < DOUBLE_INDIRECT_BLOCK_POINTERS) {
    /* Gets inode Double Indirect block */
    int double_index_first = 0;
    int double_index_second = end_of_file;
    block_sector_t double_pointers_first[INDIRECT_BLOCK_POINTERS];
    if(inode->double_indirect_block == NULL) {
      bool temp = free_map_allocate(1, &inode->double_indirect_block);
      if(!temp) {
        return 0;
      }
      block_write(fs_device, inode->double_indirect_block, zero_out);
      double_index_second = 0;
    }
    else
      block_read(fs_device, inode->double_indirect_block,
                                                      &double_pointers_first);

    /* Goes through double array, finds indirect blocks, if no indirect blocks
      are found, make one */
    block_sector_t double_pointers_second[INDIRECT_BLOCK_POINTERS];
    while(sectors > 0 && end_of_file < DOUBLE_INDIRECT_BLOCK_POINTERS) {
      /* allocate indirect blocks from double array */
      if(double_pointers_first[double_index_first] == NULL) {
        /* Creates indiret block */
        bool temp = free_map_allocate(1,
                                   &double_pointers_first[double_index_first]);
        if(!temp) {
          return 0;
        }
        block_write(fs_device, double_pointers_first[double_index_first],
                                                                  zero_out);
      }
      else
        block_read(fs_device, double_pointers_first[double_index_first],
                                                      &double_pointers_second);


      /* allocate in free map and write zeros */
      while(sectors > 0 && double_index_second < INDIRECT_BLOCK_POINTERS) {
        bool temp = free_map_allocate(1,
                                  &double_pointers_second[double_index_second]);
        if(!temp) {
          return 0;
        }
        block_write(fs_device, double_pointers_second[double_index_second],
                                                                     zero_out);
        double_index_second++;
        sectors--;
      }
      block_write(fs_device, double_pointers_first[double_index_first],
                                                      &double_pointers_second);
      end_of_file -= INDIRECT_BLOCK_POINTERS;
      double_index_first++;
      if(double_index_first == 128) {
        return 0;
      }
    }
    block_write(fs_device, inode->double_indirect_block,
                                                       &double_pointers_first);

  }
  lock_release(&inode->lock);
  if(sectors != 0) {
    return 0;
  }
  return 1;
}


bool
inode_allocate(struct inode_disk *inode_disk) {
  off_t length = inode_disk->length;
  char zero_out[512];
  int i;
  for (i=0;i<512;i++){
    zero_out[i]=0;
  }
  int sectors = bytes_to_sectors(length);

  if(sectors == 0) {
    return 1;
  }
  /* allocate direct */
  int direct_block_number = 0;
  while(sectors != 0 && direct_block_number < DIRECT_BLOCK) {
    bool temp = free_map_allocate(1,
                              &inode_disk->direct_blocks[direct_block_number]);
    if(!temp) {
      return 0;
    }
    block_write(fs_device, inode_disk->direct_blocks[direct_block_number],
                                                                     zero_out);
    direct_block_number++;
    sectors--;
  }

  /* allocate indirect if needed */
  if(sectors != 0) {
  /* check if it has indirect */
    block_sector_t indirect_pointers[INDIRECT_BLOCK_POINTERS];
    bool temp = free_map_allocate(1, &inode_disk->indirect_block);
    if(!temp) {
      return 0;
    }
    block_write(fs_device, inode_disk->indirect_block, zero_out);

    /* allocate direct blocks to indirect */
    int indirect_block_number = 0;
    while(sectors > 0 && indirect_block_number < INDIRECT_BLOCK_POINTERS) {
      bool temp = free_map_allocate(1,
                                   &indirect_pointers[indirect_block_number]);
      if(!temp) {
        return 0;
      }
      block_write(fs_device, indirect_pointers[indirect_block_number],
                                                                   zero_out);
      indirect_block_number++;
      sectors--;
    }
    /* Updates info to indirect block */
    block_write(fs_device, inode_disk->indirect_block, &indirect_pointers);
  }


  /* Allocates double block if needed */
  if(sectors != 0) {

    block_sector_t double_pointers_first[INDIRECT_BLOCK_POINTERS];
    bool temp = free_map_allocate(1, &inode_disk->double_indirect_block);
    if(!temp) {
      return 0;
    }
    block_write(fs_device, inode_disk->double_indirect_block, zero_out);

    /* allocate each indirect block needed from double */
    int double_index_first = 0;
    block_sector_t double_pointers_second[INDIRECT_BLOCK_POINTERS];
    while(sectors > 0) {
      /* create indiret block */
      bool temp = free_map_allocate(1,
                                   &double_pointers_first[double_index_first]);
      if(!temp) {
        return 0;
      }
      block_write(fs_device, double_pointers_first[double_index_first],
                                                                    zero_out);

      /* allocate in free map and write zeros */
      int double_index_second = 0;
      while(sectors > 0 && double_index_second < INDIRECT_BLOCK_POINTERS) {
        bool temp = free_map_allocate(1,
                                 &double_pointers_second[double_index_second]);
        if(!temp) {
          return 0;
        }
        block_write(fs_device, double_pointers_second[double_index_second],
                                                                      zero_out);
        double_index_second++;
        sectors--;
      }
      block_write(fs_device, double_pointers_first[double_index_first],
                                                       &double_pointers_second);
    }
    block_write(fs_device, inode_disk->double_indirect_block,
                                                        &double_pointers_first);
  }

  if(sectors > 0) {
    return 0;
  }
  return 1;
}

/* Dealocates the free_map located in the given inode */
bool
inode_clear (struct inode *inode) {

  int sectors = bytes_to_sectors(inode->length);
  if(sectors==0){
    return 1;
  }
  /* index where to start */
  int direct_block_number = 0;
  /* direct blocks */
  while(sectors != 0 && direct_block_number < DIRECT_BLOCK) {
    free_map_release(inode->direct_blocks[direct_block_number], 1);
    direct_block_number++;
    sectors--;
  }
  /* indirect */
  int indirect_block_number = 0;
  if(sectors != 0 && indirect_block_number < (INDIRECT_BLOCK_POINTERS)) {
    /* check if it has indirect */
    block_sector_t indirect_pointers[INDIRECT_BLOCK_POINTERS];
    block_read(fs_device, inode->indirect_block, &indirect_pointers);

    /* loop through indirect array, allocate direct blocks */
    while(sectors != 0 && indirect_block_number < INDIRECT_BLOCK_POINTERS) {
      free_map_release(indirect_pointers[indirect_block_number], 1);
      indirect_block_number++;
      sectors--;
    }
    /* do we need to write again? */
    free_map_release(inode->indirect_block, 1);
  }

  /* double indirect blocks */
  int double_index_first = 0;
  if(sectors != 0 && double_index_first < DOUBLE_INDIRECT_BLOCK_POINTERS) {
    /* get inode double indirect block */
    int double_index_second = 0;
    block_sector_t double_pointers_first[INDIRECT_BLOCK_POINTERS];
    block_read(fs_device, inode->double_indirect_block,
                                                      &double_pointers_first);

    /* go through double array, find indirect blocks, if none, make it */
    block_sector_t double_pointers_second[INDIRECT_BLOCK_POINTERS];
    while(sectors != 0 && double_index_second < DOUBLE_INDIRECT_BLOCK_POINTERS)
    {
      /* read double index */
      block_read(fs_device, double_pointers_first[double_index_first],
                                                      &double_pointers_second);

      /* allocate in free map and write zeros */
      while(sectors > 0 && double_index_second < INDIRECT_BLOCK_POINTERS) {
        free_map_release(double_pointers_second[double_index_second], 1);
        double_index_second++;
        sectors--;
      }
      free_map_release(double_pointers_first[double_index_first], 1);
      double_index_first++;
    }
    free_map_release(inode->double_indirect_block, 1);

  }
  if(sectors != 0) {
    return 0;
  }
  return 1;
}












/* end buffer */
