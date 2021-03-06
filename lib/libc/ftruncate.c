/*
 * truncate.c
 * truncate, ftruncate
 * implementation that substitude glibc stub implementation
 *
 * Copyright (c) 2013, LiteStack, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#include "zcalls_zrt.h"
#include "zcalls.h"
#include "zrtlog.h"
#include "zrt_helper_macros.h"
#include "zrt_check.h"
#include "transparent_mount.h"
#include "mount_specific_interface.h"
#include "mounts_manager.h"
#include "mounts_interface.h"
#include "utils.h"

#define LSEEK_SET_ASSERT_IF_FAIL(fd, length){			\
	/*set file size and test correctness of lseek result*/	\
	off_t new_pos = lseek( fd, (off_t)length, SEEK_SET);	\
	assert(new_pos==(off_t)length);				\
    }

#define CHECK_NON_NEGATIVE_VALUE_RETURN_ERROR(check_val)	\
    if ( check_val < 0 ) return check_val; /*error return*/

/*write into file length null bytes starting from current pos*/
static int write_file_padding(int fd, off_t length){
    #define BUFSIZE 0x10000
    char buffer[BUFSIZE];
    memset(buffer, '\0', BUFSIZE);
    ssize_t wrote;
    off_t remain_bytes=length;
    do{
	wrote=write(fd, buffer, remain_bytes>BUFSIZE?BUFSIZE:remain_bytes);
	remain_bytes = wrote>0? remain_bytes-wrote: wrote;
    }while(remain_bytes>0);
    /*remain_bytes holds -1 if error occured, or 0 if all correct*/
    return remain_bytes;
}


/*************************************************************************
 * Implementation used by glibc, through zcall interface; It's not using weak alias;
 **************************************************************************/

int zrt_zcall_ftruncate(int fd, off_t length){
    CHECK_EXIT_IF_ZRT_NOT_READY;

    LOG_SYSCALL_START("fd=%d,length=%lld", fd, length);
    
    struct MountsPublicInterface* transpar_mount = transparent_mount();
    assert(transpar_mount);

    errno=0;
    int ret;
    off_t saved_pos;
    struct stat st;
    /*save file position*/
    saved_pos = lseek( fd, 0, SEEK_CUR);
    CHECK_NON_NEGATIVE_VALUE_RETURN_ERROR(saved_pos);

    /*get end pos of file*/
    ret = fstat(fd, &st);
    assert(ret==0);
    CHECK_NON_NEGATIVE_VALUE_RETURN_ERROR(st.st_size);

    /*if filesize should be increased then just write null bytes '\0' into*/
    if ( length > st.st_size ){
	/*set cursor to the end of file, and check assertion*/
	off_t endpos = lseek( fd, st.st_size, SEEK_SET);
	CHECK_NON_NEGATIVE_VALUE_RETURN_ERROR(st.st_size);
	assert(endpos==st.st_size);

	/*just write amount of bytes starting from end of file*/
	int res = write_file_padding(fd, length-st.st_size);
	CHECK_NON_NEGATIVE_VALUE_RETURN_ERROR(res);
    }
    else{ 
	/*truncate data if user wants to reduce filesize
	 *set new file size*/
	int res = transpar_mount->ftruncate_size(transpar_mount, fd, length);	
	CHECK_NON_NEGATIVE_VALUE_RETURN_ERROR(res);
    }

    /*restore file position, it's should stay unchanged*/
    if ( saved_pos < length ){
	LSEEK_SET_ASSERT_IF_FAIL(fd, saved_pos);
    }
    else if (saved_pos > length){
	/*it is impossible to restore oldpos because it's not valid anymore
	 *it's pointing beyond of the file*/
    }

    /*final check of real file size and expected size*/
    ret = fstat(fd, &st);
    assert(ret==0);
    ZRT_LOG(L_INFO, "real truncated file size is %lld", st.st_size );
    assert(st.st_size==length);

    LOG_SHORT_SYSCALL_FINISH( 0, "fd=%d, old_length=%lld,new_length=%lld", 
			      fd, st.st_size, length);
    return 0;
}

