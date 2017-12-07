
#include "sfs_api.h"
#include "bitmap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fuse.h>
#include <strings.h>
#include "disk_emu.h"
#include <math.h>
#include <time.h>
#define LASTNAME_FIRSTNAME_DISK "sfs_disk.disk"

#ifndef NUM_BLOCKS
#define NUM_BLOCKS 1024  //maximum number of data blocks on the disk.
#endif

#define BITMAP_ROW_SIZE (NUM_BLOCKS/8) // this essentially mimcs the number of rows we have in the bitmap. we will have 128 rows. 
#define BLOCK_SIZE 1024
#define NO_OF_INODES 100


int const superB_size_bytes		= sizeof( superblock_t ); // Find out size of superBlock
int const superB_size_blocks	= ( int )ceil( (double)sizeof( superblock_t ) / (double)BLOCK_SIZE );

int const rootDir_size_bytes	= sizeof( directory_entry ) * NO_OF_INODES ; // Find out size of superBlock
int const rootDir_size_blocks	= ( int )ceil( (double)sizeof( directory_entry ) * NO_OF_INODES / (double)BLOCK_SIZE );

int const inodeTab_size_bytes	= sizeof( inode_t ) * NO_OF_INODES ; // Find out size of superBlock
int const inodeTab_size_blocks	= ( int )ceil( (double)sizeof( inode_t )*NO_OF_INODES / (double)BLOCK_SIZE );

int currDirectory = 1; //Start at 1 because root is 0
//tables
file_descriptor 	fd_table[NO_OF_INODES];
inode_t				in_table[NO_OF_INODES];
directory_entry		rootDir[NO_OF_INODES];

superblock_t		superBlock;


int write_bytes_to_blocks_contiguous( int currAddress, int const num_bytes, void* data_buffer )
{
	int const num_blocks = ( int )ceil( (double)num_bytes / (double)BLOCK_SIZE );

	void* write_buffer = malloc( BLOCK_SIZE * num_blocks );

	memcpy( write_buffer, data_buffer, num_bytes);

	write_blocks( currAddress, num_blocks, write_buffer );

	free( write_buffer );

	return num_blocks;
}

void read_bytes_in_blocks_contiguous( int currAddress, int num_bytes, void* data_buffer )
{
	int const num_blocks = ( int )ceil( (double)num_bytes / (double)BLOCK_SIZE );

	void* read_buffer = malloc( BLOCK_SIZE * num_blocks );

	read_blocks( currAddress, num_blocks, read_buffer );

	memcpy( data_buffer, read_buffer, num_bytes);

	free( read_buffer );
}

//void flush_in_table_from_disc()
//{
//	int const address = superB_size_blocks;
//	read_bytes_in_blocks_contiguous( address, inodeTab_size_bytes, ( void* )in_table);
//}

void flush_rootDir_from_disc() 
// Assumes in_table is up to date
// Assumes rootDir does NOT use indirect pointers ( as specified by assignment )
{
	int sizeOF = rootDir_size_bytes % BLOCK_SIZE;

	if ( rootDir_size_blocks > 12 )
	{
		printf("rootDir require indirect pointer!!!\n");
		return;
	}

	for ( int i = 0; i < rootDir_size_blocks; i++ ) //read full blocks
	{
		read_bytes_in_blocks_contiguous( in_table[0].data_ptrs[ i ], BLOCK_SIZE, ( ( ( void* ) rootDir ) + BLOCK_SIZE*i )  );
	}	
	read_bytes_in_blocks_contiguous( in_table[0].data_ptrs[ rootDir_size_blocks - 1 ], sizeOF, ( ( ( void* ) rootDir ) + ( BLOCK_SIZE*( rootDir_size_blocks - 1 ) ) ) );
}

void flush_in_table_to_disc()
{
	int const address = superB_size_blocks;
	write_bytes_to_blocks_contiguous( address, inodeTab_size_bytes, ( void* )in_table);
}

void flush_rootDir_to_disc()
{
	int sizeOF = rootDir_size_bytes % BLOCK_SIZE;

	if ( rootDir_size_blocks > 12 )
	{
		printf("rootDir require indirect pointer!!!\n");
		return;
	}

	for ( int i = 0; i < rootDir_size_blocks - 1; i++ ) //read full blocks
	{
		write_bytes_to_blocks_contiguous( in_table[0].data_ptrs[ i ], BLOCK_SIZE, ( ( ( void* ) rootDir ) + BLOCK_SIZE*i )  );
	}	
	write_bytes_to_blocks_contiguous( in_table[0].data_ptrs[ rootDir_size_blocks - 1 ], sizeOF, ( ( ( void* ) rootDir ) + ( BLOCK_SIZE*( rootDir_size_blocks - 1 ) ) ) );

}

int getBlockIndex( int inode_index, int block_num )
{
	inode_t curr_inode = in_table[ inode_index ];
	if ( block_num < 12 )
	{
		return curr_inode.data_ptrs[ block_num ];
	}
	else 
	{
		int	indBuffer[ BLOCK_SIZE / 4 ];
		read_blocks( curr_inode.indirectPointer, 1, indBuffer );
		return indBuffer[ block_num - 12 ];
	}
}

void init_fdt()
{
	for( int i = 0; i < NO_OF_INODES; i++ )
	{
		fd_table[i].inodeIndex 	= -1;
		fd_table[i].inode 		= NULL;
		fd_table[i].rwptr 		= -1;
	}
}

void init_int()
{
	for( int i = 0; i < NO_OF_INODES; i++ )
	{
		in_table[ i ].mode				= -1;
    	in_table[ i ].link_cnt			= -1;
    	in_table[ i ].uid				= -1;
    	in_table[ i ].gid				= -1;
    	in_table[ i ].size 				= -1;
    	in_table[ i ].indirectPointer 	= -1;

    	for ( int j = 0; j < 12; j++ )
    	{
    		in_table[ i ].data_ptrs[ j ] = -1;
    	}
	}
}

void init_super()
{
	superBlock.magic			= 0xACBD0005;
    superBlock.block_size 		= BLOCK_SIZE;
    superBlock.fs_size			= NUM_BLOCKS;
    superBlock.inode_table_len	= NO_OF_INODES;
    superBlock.root_dir_inode	= 0;
}

void init_root()
	//initialise root directory so that we can tell whether a dir_entry is in use when creating a file
{
	for ( int i = 0; i < NO_OF_INODES; i++ )
	{
		rootDir[ i ].num  		= -1;
		rootDir[ i ].name[ 0 ] 	= '\0';
		rootDir[ i ].beenGetted	= 0;
	}
}


void mksfs(int fresh) 
{
	currDirectory = 1;
	if ( fresh == 1 )
	{
		int currAddress = 0;

		// INITS - START /////////////////////////////////////////////////////
		init_fdt();
		init_int();
		init_root();
		init_super();
		init_fresh_disk( LASTNAME_FIRSTNAME_DISK, BLOCK_SIZE, NUM_BLOCKS );
		// INITS - END ///////////////////////////////////////////////////////
		

		// WRITE SUPER BLOCK - START /////////////////////////////////////////////////////

		//uint64_t buffer[5];
		write_bytes_to_blocks_contiguous( currAddress, superB_size_bytes, ( void* )&superBlock);
		currAddress += superB_size_blocks;

		// WRITE SUPER BLOCK - END ///////////////////////////////////////////////////////

		// OCCUPY SUPER - START /////////////////////////////////////////////////////

		for ( int i = 0; i < superB_size_blocks; i++ )
		{
			get_index();
		}

		// OCCUPY SUPER - END ///////////////////////////////////////////////////////

		// PREPARE FIRST INODE - START /////////////////////////////////////////////////////

		in_table[ 0 ].size 	= rootDir_size_bytes; 
		int size_counter 	= rootDir_size_blocks;

		while( size_counter > 0 )
		{
			int diff = rootDir_size_blocks - size_counter;
			if ( diff < 12 )
			{
				in_table[ 0 ].data_ptrs[ diff ] = currAddress + inodeTab_size_blocks + diff;
			}
			size_counter--;
		}

		// PREPARE FIRST INODE - END ///////////////////////////////////////////////////////
	
		// WRITE+OCCUPY INODE TABLE BLOCKS - START /////////////////////////////////////////////////////
		
		write_bytes_to_blocks_contiguous ( currAddress, inodeTab_size_bytes, ( void* )in_table );
		currAddress += inodeTab_size_blocks;

		for ( int i = 0; i < inodeTab_size_blocks; i++ )
		{
			int test = get_index();
		}

		// WRITE+OCCUPY INODE TABLE BLOCKS - END ///////////////////////////////////////////////////////
	
		// WRITE+OCCUPY ROOTDIR BLOCKS - START /////////////////////////////////////////////////////
		
		write_bytes_to_blocks_contiguous( currAddress, rootDir_size_bytes, ( void* )rootDir );
		currAddress += rootDir_size_blocks;

		for ( int i = 0; i < rootDir_size_blocks; i++ )
		{
			get_index();
		}

		// WRITE+OCCUPY ROOTDIR BLOCKS - END ///////////////////////////////////////////////////////
		

		fd_table[ 0 ].inodeIndex = 0;
		fd_table[ 0 ].inode = &in_table[0];

		force_set_index( NUM_BLOCKS - 1 );
		write_bytes_to_blocks_contiguous( NUM_BLOCKS - 1, 128, ( void* )get_fbm_ptr() );
	}	
	else if ( fresh == 0 )
	{
		int currAddress = 0;

		init_fdt();
		init_disk( LASTNAME_FIRSTNAME_DISK, BLOCK_SIZE, NUM_BLOCKS );
		read_bytes_in_blocks_contiguous( currAddress, superB_size_bytes, ( void* )&superBlock);
		currAddress += superB_size_blocks;
		read_bytes_in_blocks_contiguous( currAddress, inodeTab_size_bytes, ( void* )in_table);
		uint8_t free_bmp_buffer[128];
		read_bytes_in_blocks_contiguous( NUM_BLOCKS - 1, 128, ( void* )free_bmp_buffer);
		overwrite_fbm( free_bmp_buffer );

		flush_rootDir_from_disc();
	}
}
int sfs_getnextfilename(char *fname)
{
	int const ini_currDir = currDirectory;
	for ( int i = ini_currDir; i < NO_OF_INODES; i++ )
	{
		if ( rootDir[ i ].name[ 0 ] &&  rootDir[ i ].beenGetted == 0 && rootDir[ i ].num != -1 )
		{
			strcpy( fname, rootDir[ i ].name );
			rootDir[ i ].beenGetted = 1;
			currDirectory = i;
			return rootDir[ i ].num;
		}
	}

	for ( int i = 0; i < ini_currDir; i++ )
	{
		if ( rootDir[ i ].name[ 0 ] &&  rootDir[ i ].beenGetted == 0 && rootDir[ i ].num != -1 )
		{
			strcpy( fname, rootDir[ i ].name );
			rootDir[ i ].beenGetted = 1;
			currDirectory = i;
			return rootDir[ i ].num;
		}
	}

	return 0; //If no files exists that have not previously been getted, return 0
}
int sfs_getfilesize(const char* path)
{
	for ( int i = 0; i < NO_OF_INODES; i++ )
	{
		if ( strcmp( path, rootDir[ i ].name ) == 0 )
		{
			return in_table[ rootDir[ i ].num ].size;
		}
	}
	return -1;
}
int sfs_fopen(char *name)
{
	// VERIFY NAME FORMAT - START /////////////////
	char* dotptr 		= strstr( name, "." );
	int name_length 	= ( dotptr ) ?  dotptr - name : strlen( name );
	int exts_length 	= ( dotptr ) ? strlen( name ) - name_length - 1 : 0;
	if ( name_length > 16 )
	{
		printf("sfs_fopen: file name too long\n");
		return -1;
	} 
	else if ( exts_length > 3 ) 
	{ 
		printf("sfs_fopen: file extension too long\n");
		return -1;
	}


	int file_index	= -1;

	// CHECK THAT FILE 'name' EXISTS - START //////////////////////////////////////////

	// 									 | ////// READ INODE TABLE FROM DISC - START //////
	// May not be in scope of Assignment | flush_in_table_from_disc(); 
	// 									 | ////// READ INODE TABLE FROM DISC - END ////////

	////// READ rootDir FROM DISC - START //////
	//flush_rootDir_from_disc();
	////// READ rootDir FROM DISC - END ////////

	////// SEARCH FOR FILE 'name' IN DIRECTORY - START ////
	for ( int i = 0; i < NO_OF_INODES; i++ ) 
	{
		if ( strcmp( rootDir[ i ].name, name ) == 0 )
		{
			file_index = i;
		}
	}
	////// SEARCH FOR FILE 'name' IN DIRECTORY - END   ////

	// CHECK THAT FILE 'name' EXISTS - END ////////////////////////////////////////////

	// IF FILE DOESNT EXIST - START //////////////////////////////////////////
	if ( file_index < 0 )
	{
		////// SEARCH FOR OPEN INODE - START ///
		int inode_index	= 0*0;
		int inode_found = 0; 
		while ( inode_found == 0 && inode_index < NO_OF_INODES - 1 )
		{
			inode_index++; //inc before checking to skip rootDir
			inode_found = ( in_table[ inode_index ].size == -1 );
		}
		////// SEARCH FOR OPEN INODE - END /////

		if ( inode_found == 0 )
		{
			printf("MAX INODES IN USE\n");
			return -1;
		}
		else 
		{
			////// SEARCH FOR OPEN DIR ENTRY - START ///
			int dir_index	= 1;
			int dir_found 	= 0; 
			while ( dir_found == 0 && dir_index < NO_OF_INODES )
			{
				dir_found = ( rootDir[ dir_index ].num == -1 );
				dir_index++; 
			} 
			dir_index--; //undo last inc
			////// SEARCH FOR OPEN DIR ENTRY - END /////

			if ( dir_found == 0 )
			{
				printf( "MAX DIR ENTRIES IN USE - unexpected error\n" );
				return -1;
			} 
			else 
			{
				// CREATE FILE - START ///////////////////////
				in_table[ inode_index ].size 				= 0; //size changed from -1 to indicate this inode is used
				rootDir[ dir_index ].num 					= inode_index;
				rootDir[ dir_index ].beenGetted 			= 0;


				strcpy( rootDir[ dir_index ].name, name );

				file_index = dir_index;
				// CREATE FILE - END /////////////////////////
			}
		}
	}
	// IF FILE DOESNT EXIST - END ////////////////////////////////////////////

	// IF FILE EXISTS - START //////////////////////////////////////////
	if ( file_index > 0 ) // NOT >= because of root dir, cant open root dir as file
	{	
		int file_not_opened = 1;
		for ( int i = 0; i < NO_OF_INODES; i++ )
		{
			if ( fd_table[i].inodeIndex == file_index && fd_table[i].inodeIndex != -1 )
			{
				file_not_opened = 0;
			}
		}

		if ( file_not_opened == 1 )
		{
			for ( int i = 0; i < NO_OF_INODES; i++ )
			{
				if ( fd_table[ i ].inodeIndex == -1 )
				{
					fd_table[ i ].inodeIndex 	= rootDir[ file_index ].num;
					fd_table[ i ].inode 		= &in_table[ rootDir[ file_index ].num ];
					fd_table[ i ].rwptr			= in_table[ rootDir[ file_index ].num ].size; //size will refer to the last byte in the data, so it is the end of the file
					file_index 					= i;
					break;
				}
			}
		}
	}
	// IF FILE EXISTS - END ////////////////////////////////////////////


	flush_in_table_to_disc();
	flush_rootDir_to_disc();

	return file_index;
}
int sfs_fclose(int fileID) 
{
	if ( fileID  >= 0 && fileID < 100 )
	{
		if( fd_table[ fileID ].inodeIndex != -1 )
		{
			fd_table[ fileID ].inodeIndex = -1;
			fd_table[ fileID ].inode = NULL;
			fd_table[ fileID ].rwptr = -1;
			return 0;
		} 
		else 
		{
			return -1;
		}
	}
	else 
	{
		return -1;
	}

}
int sfs_fread_old(int fileID, char *buf, int length) 
{
	int const inode_index 	= fd_table[ fileID ].inodeIndex;

	if ( inode_index < 1 )
	{
		//printf("%s\n", "Invalid file being writted to" );
		return -1;
	}

	char* loc_ptr = buf;
	int const loc_rwptr 	= fd_table[ fileID ].rwptr;
	int const loc_size	 	= (*fd_table[ fileID ].inode).size;
	int const new_rwptr 	= loc_rwptr + length;
	int const frst_block	= loc_rwptr / BLOCK_SIZE;
	int const last_block	= new_rwptr / BLOCK_SIZE;
	int const num_blocks 	= last_block - frst_block;
	int byte_idx 			= loc_rwptr % BLOCK_SIZE;					 	//Byte idx in block
	int loc_len				= length;

	if ( length + loc_rwptr > loc_size ) // Cannot read into parts of file whihc arent written
		return -1;

	int cplen = ( frst_block == last_block ) ? loc_len : BLOCK_SIZE - byte_idx;
	//Read from first block
	int curr_idx = getBlockIndex( inode_index, frst_block );
	char block_buffer[BLOCK_SIZE];
	read_blocks( curr_idx, 1, (void*)block_buffer );
	memcpy( loc_ptr, &block_buffer[byte_idx], cplen );

	loc_len -= cplen;
	loc_ptr += cplen;
	int curr_block = frst_block + 1;
	while( loc_len > BLOCK_SIZE )
	{
		cplen = BLOCK_SIZE;
		int curr_idx = getBlockIndex( inode_index, curr_block );
		read_blocks( curr_idx, 1, loc_ptr );

		loc_len -= cplen;
		loc_ptr += cplen;
		curr_block++;
	}

	if ( loc_len > 0 )
	{
		cplen = loc_len;
		int curr_idx = getBlockIndex( inode_index, curr_block );
		read_blocks( curr_idx, 1, block_buffer );
		memcpy( loc_ptr, block_buffer, cplen );
	}

	return length;

}
int max( int num1, int num2 ) 
{
	return ( num1 > num2 ) ? num1 : num2 ; 
}

int alloc_new_blocks ( int in_idx, int new_blocks )
{
	if ( new_blocks == 0 )
	{
		return 0;
	} 
	else if ( new_blocks < 0 )
	{
		printf("Nonpositive new_blocks.\n");
		return -1;
	}
	inode_t intemp = in_table[ in_idx ];

	int const max_blocks 	= ( 12 + ( BLOCK_SIZE / 4 ) ); 											// #data_ptrs + BLOCK_SIZE/sizeof(int)
	int const size_block	= (int)ceil( (float)intemp.size / (float)BLOCK_SIZE );				 	//Position of the end of the size's block 
	int first_block 		= size_block;
	int last_block 			= size_block + new_blocks;

	int blocks_allocated = 0; 

	if ( size_block + new_blocks >= max_blocks )
	{
		printf( "Trying to allocate too many blocks.\n" );
		return -1;
	}

	if ( size_block < 12 && last_block >= 12 ) // Need to create index block
	{
		int new_idx = get_index();
		if ( new_idx == -1 )
		{
			printf("No remaining space on disc\n");
			return -1;
		}
		int idxBlock[ BLOCK_SIZE / 4 ];
		for ( int i = 0; i < BLOCK_SIZE / 4; i++ )
		{
			idxBlock[ i ] = -1;
		} 
		write_blocks( new_idx, 1, (void*)idxBlock);
		intemp.indirectPointer = new_idx;
	}

	int* new_indices = (int*)malloc( sizeof( int ) * new_blocks); 
	for ( int i = 0; i < new_blocks; i++ )
	{
		int new_idx = get_index();
		if ( new_idx == -1 )
		{
			printf("No remaining space on disc\n");
			for ( int j = i-1; j >= 0; j-- )
			{
				rm_index( new_indices[ j ] );
			}
			free(new_indices);
			return -1;
		}
		new_indices[ i ] = new_idx;

	}

	for ( int i = first_block; i < last_block; i++ )
	{
		if ( i < 12 )
		{
			intemp.data_ptrs[ i ] = new_indices[ blocks_allocated ];
			blocks_allocated++;
		} 
		else 
		{
			int idxBlock[ BLOCK_SIZE / 4 ];
			read_blocks( intemp.indirectPointer, 1, (void*)idxBlock);
			idxBlock[ i - 12 ] = new_indices[ blocks_allocated ];
			write_blocks( intemp.indirectPointer, 1, (void*)idxBlock);
			blocks_allocated++;
		}
	}

	in_table[ in_idx ] = intemp;
	free(new_indices);
	write_bytes_to_blocks_contiguous( NUM_BLOCKS - 1, 128, ( void* )get_fbm_ptr() );
	return blocks_allocated;
}

int sfs_fread(int fileID, char *buf, int length) 
{
	int in_idx 	= fd_table[ fileID ].inodeIndex; 
	int rwptr 	= fd_table[ fileID ].rwptr;
	int loc_size 	= in_table[ in_idx ].size;
	int cplen;

	if ( rwptr >= loc_size )
	{
		printf("rwptr >= loc_size\n");
		return -1;
	}

	while ( rwptr + length > loc_size )
	{
		length--;
	}

	if ( length < 0 )
	{		
		printf("nonpositive length\n");
		return -1;
	}

	if ( in_idx < 1 ) //Cannot write to root
	{
		return -1; //Invalid fd
	}
	
	inode_t intemp = in_table[ in_idx ];

	int rwptr_block				= (int)ceil( (float)rwptr / (float)BLOCK_SIZE ) * BLOCK_SIZE; 	//Position of the end of the rwptr's block in bytes
	int len_from_rwptr_block	= length - ( rwptr_block - rwptr );								//Length from the end of the rwptr's block
	int size_block 				= (int)ceil( (float)intemp.size / (float)BLOCK_SIZE ) * BLOCK_SIZE; 	//Position of the end of the size's block in bytes

	int size_inc 				= max( 0, rwptr - intemp.size + length );						//Total Size increase
	int size_inc_from_block		= max( 0, rwptr_block - size_block + len_from_rwptr_block );	//Relevant size increase
	int num_new_blocks			= (int)ceil( (float)size_inc_from_block / (float)BLOCK_SIZE );			//Number of new blocks

	int const last_block = ( size_block / BLOCK_SIZE ) + num_new_blocks;
	int const rwptr_block_ind = rwptr / BLOCK_SIZE;
	int loc_len = length;

	// Read from first block
	char temp_buf[BLOCK_SIZE];
	int block_idx = getBlockIndex( in_idx, rwptr_block_ind );
	int byte_idx = rwptr % BLOCK_SIZE;

	read_blocks( block_idx, 1, temp_buf );
	cplen = ( loc_len > BLOCK_SIZE - byte_idx ) ? BLOCK_SIZE - byte_idx : loc_len;
	memcpy( buf, temp_buf + byte_idx, cplen);

	char* loc_buf = buf + cplen;
	loc_len -= cplen;

	for ( int i = rwptr_block_ind+1; i <= last_block && loc_len > 0; i++ )
	{
		block_idx = getBlockIndex( in_idx, i );
		read_blocks( block_idx, 1, temp_buf );
		cplen = ( loc_len > BLOCK_SIZE ) ? BLOCK_SIZE : loc_len;
		memcpy( loc_buf, temp_buf, cplen);
		loc_buf += cplen;
		loc_len -= cplen;
	}

	in_table[ fd_table[ fileID ].inodeIndex ].size 	+= size_inc;
	fd_table[ fileID ].rwptr 						+= length;
	flush_in_table_to_disc();
	return length;
}
int sfs_fwrite( int fileID, const char* buf, int length )
{
	int const in_idx 	= fd_table[ fileID ].inodeIndex;
	int const rwptr 	= fd_table[ fileID ].rwptr;

	if ( in_idx < 1 ) //Cannot write to root
	{
		return -1; //Invalid fd
	}

	inode_t intemp = in_table[ in_idx ];

	int rwptr_block				= (int)ceil( (float)rwptr / (float)BLOCK_SIZE ) * BLOCK_SIZE; 	//Position of the end of the rwptr's block in bytes
	int len_from_rwptr_block	= length - ( rwptr_block - rwptr );								//Length from the end of the rwptr's block
	int size_block 				= (int)ceil( (float)intemp.size / (float)BLOCK_SIZE ) * BLOCK_SIZE; 	//Position of the end of the size's block in bytes

	int size_inc 				= max( 0, rwptr - intemp.size + length );						//Total Size increase
	int size_inc_from_block		= max( 0, rwptr_block - size_block + len_from_rwptr_block );	//Relevant size increase
	int num_new_blocks			= (int)ceil( (float)size_inc_from_block / (float)BLOCK_SIZE );			//Number of new blocks

	//printf("Number of new_blocks of write of %d bytes from rwptr %d: %d \n", length, rwptr, num_new_blocks);

	if ( alloc_new_blocks( in_idx, num_new_blocks ) == -1 )
	{
		return -1;
	}

	int const last_block = ( size_block / BLOCK_SIZE ) + num_new_blocks;
	int const rwptr_block_ind = rwptr / BLOCK_SIZE;
	int loc_len = length;

	// Read from first block
	char temp_buf[BLOCK_SIZE];
	int block_idx = getBlockIndex( in_idx, rwptr_block_ind );
	int byte_idx = rwptr % BLOCK_SIZE;
	read_blocks( block_idx, 1, temp_buf );

	int cplen = ( loc_len > BLOCK_SIZE - byte_idx ) ? BLOCK_SIZE - byte_idx : loc_len;
	memcpy( temp_buf + byte_idx, buf, cplen);
	write_blocks( block_idx, 1, temp_buf );

	char* loc_buf = buf + BLOCK_SIZE - byte_idx;
	loc_len -= cplen;

	for ( int i = rwptr_block_ind+1; i <= last_block && loc_len > 0; i++ )
	{
		cplen = ( loc_len > BLOCK_SIZE ) ? BLOCK_SIZE : loc_len;
		block_idx = getBlockIndex( in_idx, i );
		read_blocks( block_idx, 1, temp_buf );
		memcpy( temp_buf , loc_buf, cplen);
		write_blocks( block_idx, 1, temp_buf );
		loc_buf += cplen;
		loc_len -= cplen;
	}

	in_table[ fd_table[ fileID ].inodeIndex ].size 	+= size_inc;
	fd_table[ fileID ].rwptr 						+= length;
	flush_in_table_to_disc();

	
	return length;
}

int sfs_fwrite_old( int fileID, const char* buf, int length )
{
	int const inode_index 	= fd_table[ fileID ].inodeIndex;

	if ( inode_index < 1 )
	{
		printf("%s\n", "Invalid file being writted to" );
		return -1;
	}
	char* loc_ptr = buf;
	int const loc_rwptr 	= fd_table[ fileID ].rwptr;
	int const loc_size	 	= (*fd_table[ fileID ].inode).size;
	int const new_rwptr 	= loc_rwptr + length;
	int const frst_block	= loc_rwptr / BLOCK_SIZE;
	int const last_block	= new_rwptr / BLOCK_SIZE;
	int byte_idx 			= loc_rwptr % BLOCK_SIZE;					 	//Byte idx in block
	int loc_len				= length;

	int const last_existing_block 	= loc_size / BLOCK_SIZE; 
	int create_first_block 			= ( loc_size ==  0 ) ? 1 : 0;
	
	// ALLOCATE NEW BLOCKS - START /////////////////////////////////////////////////////////////////////////////////////////////////
	int num_new_blocks 				= last_block - frst_block + create_first_block;
	//if ( loc_size == 0 ) //If the file is new, no blocks are assigned by default 
	//	num_new_blocks++;

	int* newblock_buffer = (int*)malloc( sizeof(int) * num_new_blocks );
	for ( int i = 0; i < num_new_blocks; i++ )
	{
		newblock_buffer[ i ] = get_index();
	}

	if ( last_existing_block < 12 && last_existing_block + num_new_blocks >= 12 ) //Create indirectpointer block
	{
		in_table[ inode_index ].indirectPointer = get_index();
		int idxBlock[ (int)ceil( (double)(BLOCK_SIZE) / (double)(sizeof( int ) ) ) ];
		for ( int i = 0; i < (int)ceil( (double)(BLOCK_SIZE) / (double)(sizeof( int ) ) ); i++ )
		{
			idxBlock[i] = -1;
		}
		write_blocks( in_table[ inode_index ].indirectPointer, 1, idxBlock );
	}

	int blocks_created = 0;
	int first_unalloc_block = last_existing_block + 1 - create_first_block;
	for ( int i = first_unalloc_block; i < (int)(fmin(12, num_new_blocks) ); i++ ) // Allocate new blocks to data_ptrs
	{
		if ( 1/*in_table[ inode_index ].data_ptrs[ i ] == -1*/ )
		{
			in_table[ inode_index ].data_ptrs[ i ] = newblock_buffer[ blocks_created ];
			blocks_created++;
		}
	}

	if ( blocks_created < num_new_blocks ) //Allocate new blocks to idxBlock
	{
		int idxBlock[ (int)ceil( (double)(BLOCK_SIZE) / (double)(sizeof( int ) ) ) ];
		read_blocks( in_table[ inode_index ].indirectPointer, 1, idxBlock );
		int i = first_unalloc_block + blocks_created;
		while( blocks_created < num_new_blocks )
		{
			if ( i >= (int)ceil( (double)(BLOCK_SIZE) / (double)(sizeof( int ) ) ) )
			{
				printf( "Desired write would exceed max file size\n");
				for ( int j = 0; j < num_new_blocks; j++ )
				{
					rm_index( newblock_buffer[ j ] ); //free used bits
				}			
				return -1;
			}
			if ( 1/*idxBlock[ i ] == -1*/ )
			{
				idxBlock[ i ] = newblock_buffer[ blocks_created ];
				blocks_created++;
			}
			i++;
		}

		write_blocks( in_table[ inode_index ].indirectPointer, 1, idxBlock );

	}
	free(newblock_buffer);
	// ALLOCATE NEW BLOCKS - END ///////////////////////////////////////////////////////////////////////////////////////////////////

	// WRITE DATA - START ///////////////////////////////////////////////////////////////////////////////////////////////////
	int cplen = ( frst_block == last_block ) ? loc_len : BLOCK_SIZE - byte_idx;
	int curr_idx = getBlockIndex( inode_index, frst_block );
	char data[BLOCK_SIZE];
	read_blocks( curr_idx, 1, data ); //read current block data in;
	memcpy( data + byte_idx, loc_ptr, cplen ); //copy data from buff into file data
	write_blocks( curr_idx, 1, data ); //flush data back into file system

	loc_len -= cplen; //Amount of data still to be written
	loc_ptr += cplen;
	int curr_block = frst_block+ 1;
	while( loc_len > 0 )
	{
		cplen = ( loc_len > BLOCK_SIZE ) ? BLOCK_SIZE : loc_len;
		int curr_idx = getBlockIndex( inode_index, curr_block );
		read_blocks( curr_idx, 1, data );
		memcpy( data, loc_ptr, cplen );
		write_blocks( curr_idx, 1, data ); //flush data back into file system

		loc_len -= cplen;
		loc_ptr += cplen;
		curr_block++;
	}

	// WRITE DATA - END /////////////////////////////////////////////////////////////////////////////////////////////////////
	fd_table[ fileID ].rwptr = new_rwptr;//Update file descriptor
	in_table[ inode_index ].size = ( new_rwptr > loc_size ) ? new_rwptr : loc_size;//Update file descriptor

	return length;
}
/*
int sfs_fwrite(int fileID, const char *buf, int length) 
{
	int const loc_rwptr 	= fd_table[ fileID ].rwptr;
	int const loc_size	 	= (*fd_table[ fileID ].inode).size;
	int const new_rwptr 	= loc_rwptr + length;
	int const frst_block	= loc_rwptr / BLOCK_SIZE;
	int const last_block	= new_rwptr / BLOCK_SIZE;
	int const num_blocks 	= last_block - frst_block;
	int const inode_index 	= fd_table[ fileID ].inodeIndex;


	if ( in_table[ inode_index ].data_ptrs[0] == -1 )
	{
		in_table[ inode_index ].data_ptrs[0] = get_index();
	}

	if ( num_blocks == 0 ) //All changes inside single block
	{
		int curr_idx = getBlockIndex( inode_index, frst_block ); 	//Block index in fs
		int byte_idx = loc_rwptr % BLOCK_SIZE;					 	//Byte idx in block
		
		char block_buffer[ BLOCK_SIZE ];							
		read_blocks( curr_idx, 1, block_buffer );					// read curr data

		memcpy( block_buffer + byte_idx, buf, length );				// overwrite new data in local buffer
		write_blocks( curr_idx, 1, block_buffer );					// write changes to disc
	}
	else if ( new_rwptr <= loc_size ) //All changes inside multiple existing blocks
	{
		int curr_idx 	= getBlockIndex( inode_index, frst_block ); 	//Block index in fs
		int byte_idx 	= loc_rwptr % BLOCK_SIZE;					 	//Byte idx in block
		int cplen		= BLOCK_SIZE - byte_idx;						//Write from rwptr to 
		int loc_len		= length;

		char block_buffer[ BLOCK_SIZE ];							
		read_blocks( curr_idx, 1, block_buffer );					// read curr data

		memcpy( block_buffer + byte_idx, buf, cplen );				// overwrite new data in local buffer
		write_blocks( curr_idx, 1, block_buffer );					// write changes to disc

		loc_len -= cplen;											// update how much left to write

		for ( int i = frst_block + 1; i < last_block; i++ )			// write to full blocks
		{
			cplen = BLOCK_SIZE;										// copy a whole block
			curr_idx = getBlockIndex( inode_index, i );
			read_blocks( curr_idx, 1, block_buffer );				// read curr data

			memcpy( block_buffer, buf, cplen );						// overwrite new data in local buffer
			write_blocks( curr_idx, 1, block_buffer );				// write changes to disc

			loc_len -= cplen;
		} 

		cplen = loc_len; 
		curr_idx = getBlockIndex( inode_index, last_block );
		read_blocks( curr_idx, 1, block_buffer );					// read curr data
		memcpy( block_buffer, buf, cplen );				// overwrite new data in local buffer
		write_blocks( curr_idx, 1, block_buffer );					// write changes to disc
	}
	else 
	{
		int curr_idx 	= getBlockIndex( inode_index, frst_block ); 	//Block index in fs
		int byte_idx 	= loc_rwptr % BLOCK_SIZE;					 	//Byte idx in block
		int cplen		= BLOCK_SIZE - byte_idx;						//Write from rwptr to 
		int loc_len		= length;

		char block_buffer[ BLOCK_SIZE ];							
		read_blocks( curr_idx, 1, block_buffer );					// read curr data

		memcpy( block_buffer + byte_idx, buf, cplen );				// overwrite new data in local buffer
		write_blocks( curr_idx, 1, block_buffer );					// write changes to disc

		loc_len -= cplen;											// update how much left to write

		int num_new_blocks = (int)ceil( (float)(new_rwptr - loc_size)/(float)BLOCK_SIZE );

		for ( int i = frst_block + 1; i < last_block - num_new_blocks; i++ ) //write into existing full blocks
		{

			cplen = BLOCK_SIZE;
			curr_idx = getBlockIndex( inode_index, i );
			read_blocks( curr_idx, 1, block_buffer );					// read curr data

			memcpy( block_buffer, buf, cplen );				// overwrite new data in local buffer
			write_blocks( curr_idx, 1, block_buffer );					// write changes to disc

			loc_len -= cplen;
		}

		for ( int i = last_block - num_blocks; i < last_block; i++ ) //write into non existing full blocks
		{
			if ( i < 12 )
			{
				cplen = loc_len; //copy remaining data

				curr_idx = get_index();
				in_table[ inode_index ].data_ptrs[ i ] 	= curr_idx;
				in_table[ inode_index ].size 			=  (in_table[ inode_index ].size / BLOCK_SIZE)*BLOCK_SIZE + BLOCK_SIZE; //new size is old size rounded to new block
				write_bytes_to_blocks_contiguous( curr_idx, cplen, block_buffer ); 
				loc_len -= cplen;
			}
			else 
			{
				if ( in_table[ inode_index ].indirectPointer == -1 )
				{
					in_table[ inode_index ].indirectPointer = get_index();
				}
				cplen = BLOCK_SIZE;
				curr_idx = get_index();

				int idxBlock[ BLOCK_SIZE / sizeof(int) ];
				read_blocks( in_table[ inode_index ].indirectPointer, 1, (void*)idxBlock );	//|
				idxBlock[ i - 12 ] = curr_idx;												//| Update index block with new pointer
				write_blocks( in_table[ inode_index ].indirectPointer, 1, (void*)idxBlock ); //|
				in_table[ inode_index ].size =  (in_table[ inode_index ].size / BLOCK_SIZE)*BLOCK_SIZE + BLOCK_SIZE; //new size is old size rounded to new block + new block
				
				write_bytes_to_blocks_contiguous( curr_idx, cplen, block_buffer ); 
				loc_len -= cplen;
			}
		}

		if ( last_block < 12 )
		{
			cplen = loc_len; //copy remaining data
			curr_idx = get_index();
			in_table[ inode_index ].data_ptrs[ last_block ] 	= curr_idx;
			in_table[ inode_index ].size 			=  (in_table[ inode_index ].size / BLOCK_SIZE)*BLOCK_SIZE + loc_len; //new size is old size rounded to new block + Remaining bytsto be copied
			write_bytes_to_blocks_contiguous( curr_idx, cplen, block_buffer ); 
			loc_len -= cplen;
		}
		else 
		{
			if ( in_table[ inode_index ].indirectPointer == -1 )
			{
				in_table[ inode_index ].indirectPointer = get_index();
			}
			cplen = BLOCK_SIZE;
			curr_idx = get_index();
			int idxBlock[ BLOCK_SIZE / sizeof(int) ];
			read_blocks( in_table[ inode_index ].indirectPointer, 1, (void*)idxBlock );		//|
			idxBlock[ last_block - 12 ] = curr_idx;											//| Update index block with new pointer
			write_blocks( in_table[ inode_index ].indirectPointer, 1, (void*)idxBlock ); 	//|
			in_table[ inode_index ].size =  (in_table[ inode_index ].size / BLOCK_SIZE)*BLOCK_SIZE + loc_len; //new size is old size rounded to new block + Remaining bytsto be copied
			
			write_bytes_to_blocks_contiguous( curr_idx, cplen, block_buffer ); 
			loc_len -= cplen;
		}
	} 
	fd_table[ fileID ].rwptr = loc_rwptr + length;
}
*/
int sfs_fseek(int fileID, int loc) 
{
	if ( fd_table[ fileID ].inodeIndex != -1 )
	{
		fd_table[ fileID ].rwptr = loc;
	}
}
int sfs_remove(char *file) 
{
	int inode_idx = -1;
	for ( int i = 0; i < NO_OF_INODES; i++ )
	{
		if ( strcmp( rootDir[ i ].name, file ) == 0 )
		{
			inode_idx = rootDir[ i ].num;
		}
	}

	if ( inode_idx == -1 )
	{
		printf("File not found\n");
		return -1;
	}
	else 
	{
		// FREE DATA INDICES - START ////////////////////////////////////////////////////////
		inode_t inode = in_table[ inode_idx ];
		for ( int i = 0; i < 12; i++ )
		{
			rm_index( inode.data_ptrs[ i ] );		//|Free data_ptr indices
			inode.data_ptrs[ i ] = -1;				//|
		}

		if ( inode.indirectPointer != -1 )
		{
			int idxBlock[1024/4];							//|
			read_blocks( inode.indirectPointer, 1, idxBlock );		//|
			for ( int i = 0; i < 1024 / 4; i++ )
			{
				if ( idxBlock[ i ] != -1 )
				{
					rm_index( idxBlock[ i ] );		//|Free idxBlock indices
				}
			}
		}
		// FREE DATA INDICES - END //////////////////////////////////////////////////////////
		for ( int i = 0; i < NO_OF_INODES; i++ )
		{
			if ( fd_table[ i ].inodeIndex == inode_idx )
			{
				fd_table[ i ].inodeIndex 	= -1;  		//|
				fd_table[ i ].inode 		= NULL;		//| Update fd_table (force removed files to be closed)
				fd_table[ i ].rwptr 		= -1;		//|
			}
		}

		for ( int i = 0; i < NO_OF_INODES; i++ )
		{
			if ( rootDir[ i ].num == inode_idx )
			{
				rootDir[ i ].num = -1;
				rootDir[ i ].name[ 0 ] = '\0';
				rootDir[ i ].beenGetted = 0;
			}
		}
	}
}

void test_print_block_indices( int in_idx, int num_blocks )
{
	printf("in_table[i] data ptrs:\n");
	inode_t intemp = in_table[ in_idx ];
	for ( int i = 0; i < num_blocks; i++ )
	{
		if ( i < 12 )
		{
			printf( " %d", intemp.data_ptrs[ i ] );
		} 
		else 
		{
			int idxBlock[ BLOCK_SIZE / 4 ];
			read_blocks( intemp.indirectPointer, 1, (void*)idxBlock);
			printf( ", %d", idxBlock[ i - 12 ]  );
		}
	}
	printf("\n");
}

int test_read_after_write( int fd, char* ori_buff, int write_size )
{
	char* buff = (char*)malloc( write_size );
	fd_table[ fd ].rwptr = fd_table[ fd ].rwptr - write_size;
	int check = 0;

	sfs_fread( fd, buff, write_size );
	for ( int i = 0; i < write_size; i++ )
	{
		if ( ori_buff[ i ] != buff[ i ] )
		{
			check++;
		} 
	}
 	if ( check != 0 ) 
 	{
 		printf( "%d W/R error in file %d, after r/w %d bytes \n\n", check, fd, write_size );
 	}
 	else
 	{
 		printf( "NO W/R error in file %d, after r/w %d bytes \n\n", fd, write_size );
 	}
 	
 	free(buff);
 	return check;

}

void set_buffer( char* buff, int size, int value )
{
	for ( int i = 0; i < 24 * 1024 ; i++ )
	{
		buff[i] = ( char )value;//(i % 256);
	} 
}

void sfs_test_milan()
{
	srand(time(NULL));
	mksfs(1); 
	int file1 = sfs_fopen("File1.txt");

	char buff[24*1024];
	int num_blocks = 0;
	char read_buff[BLOCK_SIZE*24];

	set_buffer( buff, 24*1024, rand() % 256 );
	sfs_fwrite( file1, buff, 512 );
	num_blocks += 1;
	test_print_block_indices( fd_table[ file1 ].inodeIndex, num_blocks );
	test_read_after_write( file1, buff, 512 );


	set_buffer( buff, 24*1024, rand() % 256 );
	sfs_fwrite( file1, buff, 256 );
	num_blocks += 0;
	test_print_block_indices( fd_table[ file1 ].inodeIndex, num_blocks );
	test_read_after_write( file1, buff, 256 );

	set_buffer( buff, 24*1024, rand() % 256 );
	sfs_fwrite( file1, buff, 256 );
	num_blocks += 0;
	test_print_block_indices( fd_table[ file1 ].inodeIndex, num_blocks );
	test_read_after_write( file1, buff, 256 );

	set_buffer( buff, 24*1024, rand() % 256 );
	sfs_fwrite( file1, buff, 1024 );
	num_blocks += 1;
	test_print_block_indices( fd_table[ file1 ].inodeIndex, num_blocks );
	test_read_after_write( file1, buff, 1024 );

	set_buffer( buff, 24*1024, rand() % 256 );
	sfs_fwrite( file1, buff, 1024*12 );
	num_blocks += 12;
	test_print_block_indices( fd_table[ file1 ].inodeIndex, num_blocks );
	test_read_after_write( file1, buff, 1024*12 );

	set_buffer( buff, 24*1024, rand() % 256 );
	sfs_fwrite( file1, buff, 1024*12 );
	num_blocks += 12;
	test_print_block_indices( fd_table[ file1 ].inodeIndex, num_blocks );
	test_read_after_write( file1, buff, 512 );

	set_buffer( buff, 24*1024, rand() % 256 );
	sfs_fwrite( file1, buff, 1024*12 );
	num_blocks += 12;
	test_print_block_indices( fd_table[ file1 ].inodeIndex, num_blocks );
	test_read_after_write( file1, buff, 1024 );

	set_buffer( buff, 24*1024, rand() % 256 );
	sfs_fseek( file1, 0 );
	printf("Seek file %d to loc %d\n", file1, 0 );

	set_buffer( buff, 24*1024, rand() % 256 );
	sfs_fwrite( file1, buff, 512 );
	num_blocks += 0;
	test_print_block_indices( fd_table[ file1 ].inodeIndex, num_blocks );
	test_read_after_write( file1, buff, 512 );

	set_buffer( buff, 24*1024, rand() % 256 );
	sfs_fwrite( file1, buff, 256 );
	num_blocks += 0;
	test_print_block_indices( fd_table[ file1 ].inodeIndex, num_blocks );
	test_read_after_write( file1, buff, 256 );

	set_buffer( buff, 24*1024, rand() % 256 );
	sfs_fwrite( file1, buff, 256 );
	num_blocks += 0;
	test_print_block_indices( fd_table[ file1 ].inodeIndex, num_blocks );
	test_read_after_write( file1, buff, 256 );

	set_buffer( buff, 24*1024, rand() % 256 );
	sfs_fwrite( file1, buff, 1024 );
	num_blocks += 0;
	test_print_block_indices( fd_table[ file1 ].inodeIndex, num_blocks );
	test_read_after_write( file1, buff, 1024 );

	set_buffer( buff, 24*1024, rand() % 256 );
	sfs_fwrite( file1, buff, 1024*11.5f );
	num_blocks += 0;
	test_print_block_indices( fd_table[ file1 ].inodeIndex, num_blocks );
	test_read_after_write( file1, buff, 1024*11.5f );

	set_buffer( buff, 24*1024, rand() % 256 );
	sfs_fwrite( file1, buff, 1024 );
	num_blocks += 1;
	test_print_block_indices( fd_table[ file1 ].inodeIndex, num_blocks );
	test_read_after_write( file1, buff, 1024 );

	set_buffer( buff, 24*1024, rand() % 256 );
	sfs_fwrite( file1, buff, 1024 );
	num_blocks += 1;
	test_print_block_indices( fd_table[ file1 ].inodeIndex, num_blocks );
	test_read_after_write( file1, buff, 1024 );

	set_buffer( buff, 24*1024, rand() % 256 );
	sfs_fwrite( file1, buff, 1024 );
	num_blocks += 1;
	test_print_block_indices( fd_table[ file1 ].inodeIndex, num_blocks );
	test_read_after_write( file1, buff, 1024 );

	set_buffer( buff, 24*1024, rand() % 256 );
	sfs_fwrite( file1, buff, 1024 );
	num_blocks += 1;
	test_print_block_indices( fd_table[ file1 ].inodeIndex, num_blocks );
	test_read_after_write( file1, buff, 1024 );

	sfs_fseek( file1, 0 );
	
/*
	int idxBlock[1024/4];
	read_blocks( ( *fd_table[ file1 ].inode ).indirectPointer, 1, idxBlock );

	for ( int i = 0; i < 24 * 1024 ; i++ )
	{
		buff[i] = ( char )49;
	} 

	//for ( int i = 0; i < 12; i ++ )
	//{
	//	read_blocks( ( *fd_table[ file1 ].inode ).data_ptrs[i], 1, (void*)&buff[i*1024] );
	//}
	sfs_fseek( file1, 0 );
	sfs_fread( file1, buff, 1024*24 );

	printf("\n" );
*/

}

