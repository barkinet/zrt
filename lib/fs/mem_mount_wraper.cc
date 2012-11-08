/*
 * mem_mount.c
 *
 *  Created on: 20.09.2012
 *      Author: yaroslav
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdarg.h>

extern "C" {
#include "zrtlog.h"
#include "zrt_helper_macros.h"
#include "channels_mount.h"
}
#include "mem_mount_wraper.h"
#include "nacl-mounts/memory/MemMount.h"
#include "nacl-mounts/util/Path.h"
#include "handle_allocator.h"

static MemMount* s_mem_mount_cpp = NULL;
static struct HandleAllocator* s_handle_allocator = NULL;
static struct MountsInterface* s_this=NULL;

static const char* name_from_path( std::string path ){
    /*retrieve directory name, and compare name length with max available*/
    size_t pos=path.rfind ( '/' );
    int namelen = 0;
    if ( pos != std::string::npos ){
	namelen = path.length() -(pos+1);
	return path.c_str()+path.length()-namelen;
    }
    return NULL;
}

static int is_dir( ino_t inode ){
    struct stat st;
    int ret = s_mem_mount_cpp->Stat( inode, &st );
    assert( ret == 0 );
    if ( S_ISDIR(st.st_mode) )
	return 1;
    else
	return 0;
}

static ssize_t get_file_len(ino_t node) {
    struct stat st;
    if (0 != s_mem_mount_cpp->Stat(node, &st)) {
	return -1;
    }
    return (ssize_t) st.st_size;
}

static int mem_chown(const char* path, uid_t owner, gid_t group){
    struct stat st;
    int ret = s_mem_mount_cpp->GetNode( path, &st);
    if ( ret == 0 )
	return s_mem_mount_cpp->Chown( st.st_ino, owner, group);
    else
	return ret;
}

static int mem_chmod(const char* path, uint32_t mode){
    struct stat st;
    int ret = s_mem_mount_cpp->GetNode( path, &st);
    if ( ret == 0 )
	return s_mem_mount_cpp->Chmod( st.st_ino, mode);
    else
	return ret;
}

static int mem_stat(const char* path, struct stat *buf){
    struct stat st;
    int ret = s_mem_mount_cpp->GetNode( path, &st);
    if ( ret == 0 )
	return s_mem_mount_cpp->Stat( st.st_ino, buf);
    else
	return ret;
}

static int mem_mkdir(const char* path, uint32_t mode){
    int ret = s_mem_mount_cpp->GetNode( path, NULL);
    if ( ret == 0 || (ret == -1&&errno==ENOENT) )
	return s_mem_mount_cpp->Mkdir( path, mode, NULL);
    else{
	return ret;
    }
}


static int mem_rmdir(const char* path){
    struct stat st;
    int ret = s_mem_mount_cpp->GetNode( path, &st);
    if ( ret == 0 ){
	const char* name = name_from_path(path);
	ZRT_LOG( L_EXTRA, "name=%s", name );
	if ( name && !strcmp(name, ".\0")  ){
	    /*should be specified real path for rmdir*/
	    SET_ERRNO(EINVAL);
	    return -1;
	}
	return s_mem_mount_cpp->Rmdir( st.st_ino );
    }
    else
	return ret;
}

static int mem_umount(const char* path){
    SET_ERRNO(ENOSYS);
    return -1;
}

static int mem_mount(const char* path, void *mount){
    SET_ERRNO(ENOSYS);
    return -1;
}

static ssize_t mem_read(int fd, void *buf, size_t nbyte){
    ino_t node;
    /*search inode by file descriptor*/
    int ret = s_handle_allocator->get_inode( fd, &node );
    if ( ret == 0 ){
	off_t offset;
	ret = s_handle_allocator->get_offset( fd, &offset );
	assert( ret == 0 );
	ssize_t readed = s_mem_mount_cpp->Read( node, offset, buf, nbyte );
	offset += readed;
	/*update offset*/
	ret = s_handle_allocator->set_offset( fd, offset );
	assert( ret == 0 );
	return readed;
    }
    else{
	SET_ERRNO(EBADF);
	return -1;
    }
}

static ssize_t mem_write(int fd, const void *buf, size_t nbyte){
    ino_t node;
    int ret = s_handle_allocator->get_inode( fd, &node );
    if ( ret == 0 ){
	off_t offset;
	ret = s_handle_allocator->get_offset( fd, &offset );
	assert( ret == 0 );
	ssize_t wrote = s_mem_mount_cpp->Write( node, offset, buf, nbyte );
	offset += wrote;
	ret = s_handle_allocator->set_offset( fd, offset );
	assert( ret == 0 );
	return wrote;
    }
    else{
	SET_ERRNO(EBADF);
	return -1;
    }
}

static int mem_fchown(int fd, uid_t owner, gid_t group){
    ino_t node;
    int ret = s_handle_allocator->get_inode( fd, &node );
    if ( ret == 0 ){
	return s_mem_mount_cpp->Chown( node, owner, group);
    }
    else{
	SET_ERRNO(EBADF);
	return -1;
    }
}

static int mem_fchmod(int fd, uint32_t mode){
    ino_t node;
    int ret = s_handle_allocator->get_inode( fd, &node );
    if ( ret == 0 ){
	return s_mem_mount_cpp->Chmod( node, mode);
    }
    else{
	SET_ERRNO(EBADF);
	return -1;
    }
}


static int mem_fstat(int fd, struct stat *buf){
    ino_t node;
    int ret = s_handle_allocator->get_inode( fd, &node );
    if ( ret == 0 ){
	return s_mem_mount_cpp->Stat( node, buf);
    }
    else{
	SET_ERRNO(EBADF);
	return -1;
    }
}

static int mem_getdents(int fd, void *buf, unsigned int count){
    ino_t inode;
    int ret = s_handle_allocator->get_inode( fd, &inode );
    if ( ret == 0 ){
	off_t offset;
	ret = s_handle_allocator->get_offset( fd, &offset );
	assert( ret == 0 );
	ssize_t readed = s_mem_mount_cpp->Getdents( inode, offset, (DIRENT*)buf, count);
	if ( readed != -1 ){
	    offset += readed;
	    ret = s_handle_allocator->set_offset( fd, offset );
	    assert( ret == 0 );
	}
	return readed;
    }
    else{
	SET_ERRNO(EBADF);
	return -1;
    }
}

static int mem_fsync(int fd){
    errno=ENOSYS;
    return -1;
}

static int mem_close(int fd){
    ino_t node;
    int ret = s_handle_allocator->get_inode( fd, &node );
    if ( ret == 0 ){
	ret = s_handle_allocator->free_handle(fd);
	assert( ret == 0 );
	return 0;
    }
    else{
	SET_ERRNO(EBADF);
	return -1;
    }
}

static off_t mem_lseek(int fd, off_t offset, int whence){
    ino_t node;
    int ret = s_handle_allocator->get_inode( fd, &node );
    if ( ret == 0 && !is_dir(node) ){
	off_t next;
	ret = s_handle_allocator->get_offset(fd, &next );
	assert( ret == 0 );
	ssize_t len;

	switch (whence) {
	case SEEK_SET:
	    next = offset;
	    break;
	case SEEK_CUR:
	    next += offset;
	    // TODO(arbenson): handle EOVERFLOW if too big.
	    break;
	case SEEK_END:
	    // TODO(krasin, arbenson): FileHandle should store file len.
	    len = get_file_len( node );
	    if (len == -1) {
		return -1;
	    }
	    next = static_cast<size_t>(len) - offset;
	    // TODO(arbenson): handle EOVERFLOW if too big.
	    break;
	default:
	    SET_ERRNO(EINVAL);
	    return -1;
	}
	// Must not seek beyond the front of the file.
	if (next < 0) {
	    SET_ERRNO(EINVAL);
	    return -1;
	}
	// Go to the new offset.
	ret = s_handle_allocator->set_offset(fd, next );
	assert( ret == 0 );
	return next;
    }
    else{
	SET_ERRNO(EBADF);
	return -1;
    }
}

static int mem_open(const char* path, int oflag, uint32_t mode){
    struct stat st;

    if (oflag & O_CREAT) {
	/*file should be created, using O_CREAT flag/*/
	if (0 == s_mem_mount_cpp->Creat(path, mode, &st)) {
	    ZRT_LOG(L_SHORT, "%s Creat OK", path);
	} else if ((errno != EEXIST) || (oflag & O_EXCL)) {
	    /* file/dir create error */
	    ZRT_LOG(L_ERROR, "%s Creat error", path);
	    return -1;
	}else{
	    errno=0;
	    if (0 != s_mem_mount_cpp->GetNode(path, &st)) {
		/*if GetNode dont raised specifiec errno then set generic*/
		if ( errno == 0  ){
		    SET_ERRNO(ENOENT);
		}
		return -1;
	    }
	}
    }

    /* get node from memory FS for specified type, if no errors occured 
     * then file allocated in memory FS and require file desc - fd*/
    int ret = s_mem_mount_cpp->GetNode( path, &st);
    if ( ret == 0 ){
	/*ask for file descriptor in handle allocator*/
	int fd = s_handle_allocator->allocate_handle( s_this );
	if ( fd < 0 ){
	    SET_ERRNO(ENFILE);
	    return -1;
	}
	
	/* As inode and fd are depend each form other, we need update 
	 * inode in handle allocator and stay them linked*/
	ret = s_handle_allocator->set_inode( fd, st.st_ino );
	ZRT_LOG(L_EXTRA, "errcode ret=%d", ret );
	assert( ret == 0 );

	/*append feature support is simple*/
	if ( oflag & O_APPEND ){
	    ZRT_LOG(L_INFO, "handle flag: %s", "O_APPEND");
	    mem_lseek(fd, 0, SEEK_END);
	}

	/*file truncate support, only for writable files, reset size*/
	int accmode = mode & O_ACCMODE; /*reset permission flags for accmode*/
	if ( oflag&O_TRUNC && (accmode == O_RDWR || accmode == O_WRONLY) ){
	    /*reset file size*/
	    MemNode* mnode = s_mem_mount_cpp->ToMemNode(st.st_ino);
	    if (mnode){ 
		ZRT_LOG(L_INFO, "handle flag: %s", "O_TRUNC");
		mnode->set_len(0);
	    }
	}

	return fd;
    }
    else
	return -1;
}

static int mem_fcntl(int fd, int cmd, ...){
    ino_t node;
    int ret = s_handle_allocator->get_inode( fd, &node );
    if ( ret == 0 && !is_dir(node) ){
	int retcode;
	va_list args;
	va_start(args, cmd);
	retcode = s_mem_mount_cpp->Fcntl(node, cmd, args);
	va_end(args);
	return retcode;
    }
    else{
	SET_ERRNO(EBADF);
	return -1;
    }
}

static int mem_remove(const char* path){
    return s_mem_mount_cpp->Unlink(path);
}

static int mem_unlink(const char* path){
    return s_mem_mount_cpp->Unlink(path);
}

static int mem_access(const char* path, int amode){
    return -1;
}

static int mem_isatty(int fd){
    return -1;
}

static int mem_dup(int oldfd){
    return -1;
}

static int mem_dup2(int oldfd, int newfd){
    return -1;
}

static int mem_link(const char* path1, const char* path2){
    return -1;
}

static struct MountsInterface s_mem_mount_wraper = {
    mem_chown,
    mem_chmod,
    mem_stat,
    mem_mkdir,
    mem_rmdir,
    mem_umount,
    mem_mount,
    mem_read,
    mem_write,
    mem_fchown,
    mem_fchmod,
    mem_fstat,
    mem_getdents,
    mem_fsync,
    mem_close,
    mem_lseek,
    mem_open,
    mem_fcntl,
    mem_remove,
    mem_unlink,
    mem_access,
    mem_isatty,
    mem_dup,
    mem_dup2,
    mem_link,
    EMemMountId
};

struct MountsInterface* alloc_mem_mount( struct HandleAllocator* handle_allocator ){
    s_handle_allocator = handle_allocator;
    s_mem_mount_cpp = new MemMount;
    s_this = &s_mem_mount_wraper;
    return &s_mem_mount_wraper;
}
