/*
 * Here is defined query_zcalls the entry point to ZRT code from LIBC. The zcall interfaces initialization does here;
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

//#define ZLIBC_STUB
/* Define ZLIBC_STUB to create empty implementation of zcalls interface.
 * It's needed while building ZLIBC to cut off the rest of ZRT library code.
 * For generic ZRT using it is should no be defined;*/

static void zcall_stub(void){}

#ifndef ZLIBC_STUB
#  include "zrt.h"
#  include "zcalls.h"

static struct zcalls_init_t KZcalls_init = {
    zrt_zcall_prolog_preinit,
    zrt_zcall_prolog_init,
    zrt_zcall_prolog_exit,
    zrt_zcall_prolog_gettod,
    zrt_zcall_prolog_clock,
    zrt_zcall_prolog_nanosleep,
    (void*)zcall_stub, //sched_yield
    (void*)zcall_stub, //sysconf

    zrt_zcall_prolog_close,
    zrt_zcall_prolog_dup,
    zrt_zcall_prolog_dup2,
    zrt_zcall_prolog_read,
    zrt_zcall_prolog_write,
    zrt_zcall_prolog_seek,
    zrt_zcall_prolog_fstat,
    zrt_zcall_prolog_getdents,

    zrt_zcall_prolog_open,
    zrt_zcall_prolog_stat,

    zrt_zcall_prolog_sysbrk,
    zrt_zcall_prolog_mmap,
    zrt_zcall_prolog_munmap,

    (void*)zcall_stub, //dyncode_create
    (void*)zcall_stub, //dyncode_modify
    (void*)zcall_stub, //dyncode_delete

    (void*)zcall_stub, //thread_create
    (void*)zcall_stub, //thread_exit
    (void*)zcall_stub, //thread_nice

    (void*)zcall_stub, //mutex_create,
    (void*)zcall_stub, //mutex_destroy,
    (void*)zcall_stub, //mutex_lock,
    (void*)zcall_stub, //mutex_unlock,
    (void*)zcall_stub, //mutex_trylock,

    (void*)zcall_stub, //cond_create
    (void*)zcall_stub, //cond_destroy
    (void*)zcall_stub, //cond_signal
    (void*)zcall_stub, //cond_broadcast
    (void*)zcall_stub, //cond_wait
    (void*)zcall_stub, //wait_abs

    zrt_zcall_prolog_tls_init,
    zrt_zcall_prolog_tls_get,

    (void*)zcall_stub, //open_resource,

    zrt_zcall_prolog_getres,
    zrt_zcall_prolog_gettime,

    (void*)zcall_stub, //chdir (seems it's not used)
    zrt_zcall_select,
};

static struct zcalls_zrt_t KZcalls_zrt = {
    zrt_zcall_prolog_zrt_setup,
    zrt_zcall_prolog_premain,
    zrt_zcall_prolog_postmain
};

static struct zcalls_env_args_init_t KZcalls_env_args_init = {
    zrt_zcall_prolog_nvram_read_get_args_envs,
    zrt_zcall_prolog_nvram_get_args_envs
};

static struct zcalls_nonsyscalls_t KZcalls_nonsyscalls = {
    zrt_zcall_fcntl,
    zrt_zcall_rename,
    zrt_zcall_readlink,
    zrt_zcall_symlink,
    zrt_zcall_statvfs,
    zrt_zcall_link,
    zrt_zcall_unlink,
    zrt_zcall_rmdir,
    zrt_zcall_mkdir,
    zrt_zcall_chmod,
    zrt_zcall_fchmod,
    zrt_zcall_chown,
    zrt_zcall_fchown,
    zrt_zcall_ftruncate,
    zrt_zcall_stat_realpath,
    zrt_zcall_get_phys_pages,
    zrt_zcall_get_avphys_pages,
    zrt_zcall_fchdir,
    zrt_zcall_utime,
    zrt_zcall_utimes,
    zrt_zcall_utimensat
};

#endif //ZLIBC_STUB

extern int __nacl_irt_pread(int fd, void *buf, int count, long long offset,
			    int *nread){
#ifdef ZLIBC_STUB
    return -1;
#else
    return zrt_zcall_prolog_pread(fd, buf, count, offset, nread);
#endif //ZLIBC_STUB
}

extern int __nacl_irt_pwrite(int fd, const void *buf, int count, long long offset,
			     int *nwrote){
#ifdef ZLIBC_STUB
    return -1;
#else
    return zrt_zcall_prolog_pwrite(fd, buf, count, offset, nwrote);
#endif //ZLIBC_STUB
}

int
__query_zcalls(int type, void** table )
#ifdef ZLIBC_STUB
{  return -1; }
#else
{
    int ret_type = -1;
    switch( type ){
    case ZCALLS_INIT:
	*table = &KZcalls_init;
	ret_type = type;
	break;
    case ZCALLS_ZRT:
	*table = &KZcalls_zrt;
	ret_type = type;
	break;
    case ZCALLS_NONSYSCALLS:
	*table = &KZcalls_nonsyscalls;
	ret_type = type;
	break;
    case ZCALLS_ENV_ARGS_INIT:
    	*table = &KZcalls_env_args_init;
    	ret_type = type;
    	break;
    default:
	ret_type = -1;
	break;
    }
    return ret_type;
}
#endif //ZLIBC_STUB

