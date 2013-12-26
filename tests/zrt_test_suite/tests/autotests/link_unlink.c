/*
 * Description
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

#include <dirent.h>     /* Defines DT_* constants */
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <error.h>
#include <errno.h>

#include "macro_tests.h"

#define TEST_FILE2 (TEST_FILE "2")
#define TMP_TEST_FILE "@test_1_tmp"

int match_file_inode_in_dir(const char *dirpath, const char* fname) {
    struct dirent *entry;
    DIR *dp;
    dp = opendir(dirpath);

    if (dp == NULL) {
        perror("opendir: Path does not exist or could not be read.");
        return -1;
    }

    while ( (entry = readdir(dp))){
	if ( !strcmp(fname, entry->d_name) ) return entry->d_ino;
    }

    closedir(dp);
    return -1; //no inode located
}

void test_zrt_issue_67(const char* name){
    //https://github.com/zerovm/zrt/issues/67
    /*Since unlink returns no error if file in use, then unlinked
      file should not be available for getdents/stat; but must be
      available for fstat/read/write and another functions that
      get fd/FILE as argument.*/
    int ret;
    struct stat st;
    /*open file, now it's referenced and it's means that file in use*/
    int fd = open(name, O_CREAT|O_RDWR|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
    TEST_OPERATION_RESULT( 
			  fd>=0&&errno==0,
			  &ret, ret!=0);

    /*file can be accessed as dir item via getdents */
    TEST_OPERATION_RESULT( 	match_file_inode_in_dir("/", name),
				&ret, ret!=-1);

    /*try to unlink file that in use - file removing is postponed */
    TEST_OPERATION_RESULT( unlink(name), &ret, ret==0&&errno==0);

    /*can be accessed by fd via fstat*/
    TEST_OPERATION_RESULT( fstat(fd, &st), &ret, ret==0&&errno==0);
    /*can't be accessed by name via stat*/
    CHECK_PATH_NOT_EXIST( name );
    /*can't be accessed as dir item via getdents */
    TEST_OPERATION_RESULT( 	match_file_inode_in_dir("/", name),
				&ret, ret==-1);

    errno=0;
    TEST_OPERATION_RESULT( write(fd, name, 2), &ret, ret==2&&errno==0 )
	printf("res=%d err=%d\n", ret, errno);

    /*create file with the same name as unlink'ing now, it should be valid*/
    {
	int fd2 = open(name, O_CREAT|O_RDWR|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
	TEST_OPERATION_RESULT( 
			      fd2>=0&&errno==0,
			      &ret, ret!=0);
	CHECK_PATH_EXISTANCE( name );

	close(fd2);
    }

    close(fd);
    /*file closed, from now it should not be available at all*/

    /*can't be accessed by fd via fstat*/
    TEST_OPERATION_RESULT( fstat(fd, &st), &ret, ret==-1&&errno==EBADF);
}



int main(int argc, char**argv){
    CREATE_FILE(TEST_FILE, DATA_FOR_FILE, DATASIZE_FOR_FILE);

    /*unlink*/
    REMOVE_EXISTING_FILEPATH(TEST_FILE);
    
    CREATE_FILE(TEST_FILE2, DATA_FOR_FILE, DATASIZE_FOR_FILE);

    int ret;
    TEST_OPERATION_RESULT(
			  link(TEST_FILE2, TEST_FILE),
			  &ret, ret!=-1);
    /*now we have 2 hardlinks for a single file*/
    /*compare files*/
    int fd1, fd2;
    char *data1, *data2;
    off_t filesize1, filesize2;
    MMAP_READONLY_SHARED_FILE(TEST_FILE, 0, &fd1, data1);
    MMAP_READONLY_SHARED_FILE(TEST_FILE2, 0, &fd2, data2);

    GET_FILE_SIZE(TEST_FILE, &filesize1);
    GET_FILE_SIZE(TEST_FILE2, &filesize2);
    
    TEST_OPERATION_RESULT( (filesize1==filesize2),
			   &ret, ret==1);

    CMP_MEM_DATA(data1, data2, filesize1);

    //check stat data
    struct stat st1;
    struct stat st2;
    TEST_OPERATION_RESULT(
			  stat(TEST_FILE, &st1),
			  &ret, ret==0);
    TEST_OPERATION_RESULT(
			  stat(TEST_FILE2, &st2),
			  &ret, ret==0);
    TEST_OPERATION_RESULT( 
			  st1.st_nlink==st2.st_nlink,
			  &ret, ret!=0);
    TEST_OPERATION_RESULT( 
			  st1.st_ino==st2.st_ino,
			  &ret, ret!=0);

    MUNMAP_FILE(data1, filesize1);
    MUNMAP_FILE(data2, filesize2);

    CLOSE_FILE(fd1);
    CLOSE_FILE(fd2);

    test_zrt_issue_67(TMP_TEST_FILE);
    test_zrt_issue_67(TMP_TEST_FILE);

    return 0;
}
