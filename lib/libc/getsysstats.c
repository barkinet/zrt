/*
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
#include "utils.h"
#include "memory_syscall_handlers.h"

/*************************************************************************
 * Implementation used by glibc, through zcall interface; It's not using weak alias;
 **************************************************************************/

int zrt_zcall_get_phys_pages(void){
    CHECK_EXIT_IF_ZRT_NOT_READY;
    errno=0;
    LOG_SYSCALL_START(P_TEXT, "");    

    struct MemoryManagerPublicInterface* memif = memory_interface_instance();
    long int ret = memif->get_phys_pages(memif);
    LOG_SHORT_SYSCALL_FINISH( ret, "get_phys_pages=%ld", ret );
    return ret;
}


int zrt_zcall_get_avphys_pages(void){
    CHECK_EXIT_IF_ZRT_NOT_READY;
    errno=0;
    LOG_SYSCALL_START(P_TEXT, "");    

    struct MemoryManagerPublicInterface* memif = memory_interface_instance();
    long int ret = memif->get_avphys_pages(memif);
    LOG_SHORT_SYSCALL_FINISH( ret, "get_avphys_pages=%ld", ret);
    return ret;
}
