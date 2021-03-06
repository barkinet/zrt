/*
 *
 * Copyright (c) 2012-2013, LiteStack, Inc.
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


#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <fcntl.h>
#include <dirent.h>
#include <error.h>
#include <errno.h>

#include "macro_tests.h"

void zrt_test_issue73();


int main(int argc, char **argv)
{
    int ret;
    char* nullstr = NULL;

    zrt_test_issue73();

    TEST_OPERATION_RESULT(
			  rename(nullstr, TEST_FILE),
			  &ret, ret==-1&&errno==EFAULT );

    TEST_OPERATION_RESULT(
			  rename(TEST_FILE "2", TEST_FILE),
			  &ret, ret==-1&&errno==ENOENT );

    CREATE_FILE(TEST_FILE, DATA_FOR_FILE, DATASIZE_FOR_FILE);

    TEST_OPERATION_RESULT(
			  rename(TEST_FILE, nullstr),
			  &ret, ret==-1&&errno==EFAULT );

    CHECK_PATH_EXISTANCE(TEST_FILE);

    TEST_OPERATION_RESULT(
			  rename(TEST_FILE, TEST_FILE "2"),
			  &ret, ret==0 );

    CHECK_PATH_EXISTANCE(TEST_FILE "2");

    /*try rename directory to file, file will be overwrited by directory*/
    CREATE_NON_EMPTY_DIR(DIR_NAME,DIR_FILE);
    TEST_OPERATION_RESULT(
			  rename(DIR_NAME, TEST_FILE "2"),
			  &ret, ret==0 );
    /*it's a directory now - TEST_FILE "2" */
    CHECK_PATH_EXISTANCE(TEST_FILE "2");

    /*move dir into another directory*/
    TEST_OPERATION_RESULT(
			  rename(TEST_FILE "2", DIR_NAME ),
			  &ret, ret==0 );

    CHECK_PATH_EXISTANCE(DIR_NAME);

    /*create file again on old path*/
    CREATE_FILE(TEST_FILE, DATA_FOR_FILE, DATASIZE_FOR_FILE);

    /*move file into another directory specifying full path*/
    #define FULL_DIR_FILE DIR_NAME "/" DIR_FILE
    TEST_OPERATION_RESULT(
			  rename(TEST_FILE, FULL_DIR_FILE),
			  &ret, ret==0 );

    CHECK_PATH_EXISTANCE(FULL_DIR_FILE);

    /*create file again on old path*/
    CREATE_FILE(TEST_FILE, DATA_FOR_FILE, DATASIZE_FOR_FILE);

    /*move into fake path, trying use file as directory name*/
    TEST_OPERATION_RESULT(
			  rename(TEST_FILE, FULL_DIR_FILE "/fakepath"),
			  &ret, ret==-1&&errno==ENOTDIR );
    return 0;
}

void zrt_test_issue73(){
    const char dirname[] =  "/tmp/tmp1111";
    const char newdirname[] = "/tmp/tmp2222";
    const char filename[] = "foo";
    char fullpath[PATH_MAX];
    char newfullpath[PATH_MAX];
    int ret;

    /*create file in dir*/
    snprintf(fullpath, sizeof(fullpath), "%s/%s", dirname, filename);
    TEST_OPERATION_RESULT( mkdir(dirname, 0700), &ret, ret==0&&errno==0);
    CREATE_FILE(fullpath, "xxx", 3);

    /*rename dir*/
    TEST_OPERATION_RESULT( rename(dirname, newdirname), &ret, ret==0&&errno==0);

    /*check new path & oldpath*/
    snprintf(newfullpath, sizeof(newfullpath), "%s/%s", newdirname, filename);
    CHECK_PATH_NOT_EXIST(fullpath);
    CHECK_PATH_EXISTANCE(newdirname);
    CHECK_PATH_EXISTANCE(newfullpath);
}

