/*
 * mem_mount.c
 *
 *  Created on: 20.09.2012
 *      Author: yaroslav
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <utime.h>
#include <fcntl.h>
#include <stdarg.h>

extern "C" {
#include "zrtlog.h"
#include "zrt_helper_macros.h"
}
#include "nacl-mounts/memory/MemMount.h"
#include "nacl-mounts/util/Path.h"
#include "mem_mount_wraper.h"
#include "mounts_interface.h"
#include "mount_specific_interface.h"
extern "C" {
#include "handle_allocator.h" //struct HandleAllocator, struct HandleItem
#include "open_file_description.h" //struct OpenFilesPool, struct OpenFileDescription
#include "fstab_observer.h" /*lazy mount*/
#include "fcntl_implem.h"
#include "channels_mount.h"
#include "enum_strings.h"
}

#define NODE_OBJECT_BYINODE(memount_p, inode) memount_p->ToMemNode(inode)
#define NODE_OBJECT_BYPATH(memount_p, path) memount_p->GetMemNode(path)

/*retcode -1 at fail, 0 if OK*/
#define GET_INODE_BY_HANDLE(halloc_p, handle, inode_p, retcode_p){	\
	*retcode_p = halloc_p->get_inode( handle, inode_p );		\
    }

#define GET_INODE_BY_HANDLE_OR_RAISE_ERROR(halloc_p, handle, inode_p){	\
	int ret;							\
	GET_INODE_BY_HANDLE(halloc_p, handle, inode_p, &ret);		\
	if ( ret != 0 ){						\
	    SET_ERRNO(EBADF);						\
	    return -1;							\
	}								\
    }

#define GET_STAT_BYPATH_OR_RAISE_ERROR(memount_p, path, stat_p ){	\
	int ret = memount_p->GetNode( path, stat_p);			\
	if ( ret != 0 ) return ret;					\
    }

#define HALLOCATOR_BY_MOUNT_SPECIF(mount_specific_interface_p)		\
    ((struct InMemoryMounts*)((struct MountSpecificImplem*)(mount_specific_interface_p))->mount)->handle_allocator

#define OFILESPOOL_BY_MOUNT_SPECIF(mount_specific_interface_p)		\
    ((struct InMemoryMounts*)((struct MountSpecificImplem*)(mount_specific_interface_p))->mount)->open_files_pool

#define MOUNT_INTERFACE_BY_MOUNT_SPECIF(mount_specific_interface_p)	\
    (                                                                   \
     (struct MountsPublicInterface*)(                                   \
                                     (struct MountSpecificImplem*)(mount_specific_interface_p) )->mount)

#define MEMOUNT_BY_MOUNT_SPECIF(mount_specific_interface_p)		\
    MEMOUNT_BY_MOUNT( MOUNT_INTERFACE_BY_MOUNT_SPECIF(mount_specific_interface_p) )

#define HALLOCATOR_BY_MOUNT(mount_interface_p)				\
    ((struct InMemoryMounts*)(mount_interface_p))->handle_allocator

#define OFILESPOOL_BY_MOUNT(mount_interface_p)				\
    ((struct InMemoryMounts*)(mount_interface_p))->open_files_pool

#define MEMOUNT_BY_MOUNT(mounts_interface_p) ((struct InMemoryMounts*)mounts_interface_p)->mem_mount_cpp

#define CHECK_FILE_OPEN_FLAGS_OR_RAISE_ERROR(flags, flag1, flag2)	\
    if ( (flags)!=flag1 && (flags)!=flag2 ){				\
	ZRT_LOG(L_ERROR, "file open flags must be %s or %s",		\
		STR_FILE_OPEN_FLAGS(flag1), STR_FILE_OPEN_FLAGS(flag2)); \
	SET_ERRNO( EINVAL );						\
	return -1;							\
    }


struct InMemoryMounts{
    struct MountsPublicInterface public_;
    struct HandleAllocator* handle_allocator;
    struct OpenFilesPool*   open_files_pool;
    MemMount*               mem_mount_cpp;
    struct MountSpecificPublicInterface* mount_specific_interface;
};


struct MemNode *mnode_by_handle_get_ofd_hentry(struct MountsPublicInterface *mif, 
                                               int fd, 
                                               const struct OpenFileDescription **ofd,
                                               const struct HandleItem **hentry){
    struct InMemoryMounts *mem_mount = (struct InMemoryMounts *)mif;
    if ( mem_mount->handle_allocator
         ->check_handle_is_related_to_filesystem(fd, &mem_mount->public_) == 0 ){
	*hentry = mem_mount->handle_allocator->entry(fd);
        *ofd = mem_mount->open_files_pool->entry((*hentry)->open_file_description_id);
    	return mem_mount->mem_mount_cpp->ToMemNode((*hentry)->inode);
    }
    else
        return NULL;
}


struct MemNode *mnode_by_handle(struct MountsPublicInterface *mif, int fd)
{
    struct InMemoryMounts *mem_mount = (struct InMemoryMounts *)mif;
    if ( mem_mount->handle_allocator
         ->check_handle_is_related_to_filesystem(fd, &mem_mount->public_) == 0 ){
	const struct HandleItem *hentry = mem_mount->handle_allocator->entry(fd);
    	return mem_mount->mem_mount_cpp->ToMemNode(hentry->inode);
    }
    else
        return NULL;
}



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

static int is_dir( struct MountsPublicInterface* this_, ino_t inode ){
    struct stat st;
    int ret = ((struct InMemoryMounts*)this_)->mem_mount_cpp->Stat( inode, &st );
    assert( ret == 0 );
    if ( S_ISDIR(st.st_mode) )
	return 1;
    else
	return 0;
}

static ssize_t get_file_len( struct MountsPublicInterface* this_, ino_t node) {
    struct stat st;
    if (0 != ((struct InMemoryMounts*)this_)->mem_mount_cpp->Stat(node, &st)) {
	return -1;
    }
    return (ssize_t) st.st_size;
}

static void utime_update(struct MemNode *node){
    if (node != NULL)
        node->set_utime_to_current();
}

/***********************************************************************
   implementation of MountSpecificPublicInterface as part of
   filesystem.  Below resides related functions.*/

struct MountSpecificImplem{
    struct MountSpecificPublicInterface public_;
    struct InMemoryMounts* mount;
};

/*return 0 if handle not valid, or 1 if handle is correct*/
static int check_handle(struct MountSpecificPublicInterface* this_, int handle){
    return !HALLOCATOR_BY_MOUNT_SPECIF(this_)
        ->check_handle_is_related_to_filesystem(handle, 
                                                &((struct MountSpecificImplem*)this_)->mount->public_);
}

static const char* path_handle(struct MountSpecificPublicInterface* this_, int handle){
    struct MemNode *mnode = mnode_by_handle(MOUNT_INTERFACE_BY_MOUNT_SPECIF(this_), handle);
    if ( mnode ){
        return mnode->name().c_str();
    }
    else
        return NULL;
}

static int file_status_flags(struct MountSpecificPublicInterface* this_, int fd){
    if ( HALLOCATOR_BY_MOUNT_SPECIF(this_)
	 ->check_handle_is_related_to_filesystem(fd, 
						 &((struct MountSpecificImplem*)this_)->mount->public_) == 0 ){
	const struct OpenFileDescription* ofd;
	ofd = HALLOCATOR_BY_MOUNT_SPECIF(this_)->ofd(fd);
	assert(ofd);
	return ofd->flags;
    }
    else{
	SET_ERRNO(EBADF);						\
	return -1;							\
    }
}

static int set_file_status_flags(struct MountSpecificPublicInterface* this_, int fd, int flags){
    if ( HALLOCATOR_BY_MOUNT_SPECIF(this_)
	 ->check_handle_is_related_to_filesystem(fd, 
						 &((struct MountSpecificImplem*)this_)->mount->public_) == 0 ){
	const struct HandleItem* hentry;
	hentry = HALLOCATOR_BY_MOUNT_SPECIF(this_)->entry(fd);
	OFILESPOOL_BY_MOUNT_SPECIF(this_)->set_flags( hentry->open_file_description_id,
						      flags);
	return flags;
    }
    else{
	SET_ERRNO(EBADF);
	return -1;
    }
}


/*return pointer at success, NULL if fd didn't found or flock structure has not been set*/
static const struct flock* flock_data(struct MountSpecificPublicInterface* this_, int fd ){
    struct MemNode *mnode = mnode_by_handle(MOUNT_INTERFACE_BY_MOUNT_SPECIF(this_), fd);
    if (mnode!=NULL) 
        return mnode->flock();
    else{
	SET_ERRNO(EBADF);
	return NULL;
    }
}

/*return 0 if success, -1 if fd didn't found*/
static int set_flock_data(struct MountSpecificPublicInterface* this_, int fd, const struct flock* flock_data ){
    struct MemNode *mnode = mnode_by_handle(MOUNT_INTERFACE_BY_MOUNT_SPECIF(this_), fd);
    if (mnode){
        utime_update( mnode );
        mnode->set_flock(flock_data);
        return 0;
    }
    else{
        SET_ERRNO(EBADF);
        return -1;
    }
}

static struct MountSpecificPublicInterface KMountSpecificImplem = {
    check_handle,
    path_handle,
    file_status_flags,
    set_file_status_flags,
    flock_data,
    set_flock_data
};


static struct MountSpecificPublicInterface*
mount_specific_construct( struct MountSpecificPublicInterface* specific_implem_interface,
			  struct InMemoryMounts* mount ){
    struct MountSpecificImplem* this_ = (struct MountSpecificImplem*)malloc(sizeof(struct MountSpecificImplem));
    /*set functions*/
    this_->public_ = *specific_implem_interface;
    this_->mount = mount;
    return (struct MountSpecificPublicInterface*)this_;
}


/*helpers*/

/*wraper implementation*/

static ssize_t mem_readlink(struct MountsPublicInterface* this_,
                            const char *path, char *buf, size_t bufsize){
    (void)this_;
    (void)path;
    (void)buf;
    (void)bufsize;
    SET_ERRNO(ENOSYS);
    return -1;
}
 
static int mem_symlink(struct MountsPublicInterface* this_,
                       const char *oldpath, const char *newpath){
    (void)this_;
    (void)oldpath;
    (void)newpath;
    SET_ERRNO(ENOSYS);
    return -1;
}

static int mem_chown(struct MountsPublicInterface* this_, const char* path, uid_t owner, gid_t group){
    int ret;
    struct stat st;
    GET_STAT_BYPATH_OR_RAISE_ERROR( MEMOUNT_BY_MOUNT(this_), path, &st);
    ret = MEMOUNT_BY_MOUNT(this_)->Chown( st.st_ino, owner, group);
    if ( !ret ) 
        utime_update( NODE_OBJECT_BYINODE( MEMOUNT_BY_MOUNT(this_), st.st_ino ) );
    return ret;
}

static int mem_chmod(struct MountsPublicInterface* this_, const char* path, uint32_t mode){
    int ret;
    struct stat st;
    GET_STAT_BYPATH_OR_RAISE_ERROR( MEMOUNT_BY_MOUNT(this_), path, &st);
    ret = MEMOUNT_BY_MOUNT(this_)->Chmod( st.st_ino, mode);
    if ( !ret ) 
        utime_update( NODE_OBJECT_BYINODE( MEMOUNT_BY_MOUNT(this_), st.st_ino ) );
    return ret;
}

static int mem_statvfs(struct MountsPublicInterface* this_, const char* path, struct statvfs *buf){
    (void)this_;
    (void)path;
    (void)buf;
    SET_ERRNO(ENOSYS);
    return -1;
}

static int mem_stat(struct MountsPublicInterface* this_, const char* path, struct stat *buf){
    struct stat st;
    GET_STAT_BYPATH_OR_RAISE_ERROR( MEMOUNT_BY_MOUNT(this_), path, &st);
    int ret = MEMOUNT_BY_MOUNT(this_)->Stat( st.st_ino, buf);

    /*fixes for hardlinks pseudo support, different hardlinks must have same inode,
     *but internally all nodes have separeted inodes*/
    if ( ret == 0 ){
	MemNode* node = NODE_OBJECT_BYINODE( MEMOUNT_BY_MOUNT(this_), st.st_ino);
	assert(node!=NULL);
	/*patch inode if it has hardlinks*/
	if ( node->hardinode() > 0 )
	    buf->st_ino = (ino_t)node->hardinode();
    }

    return ret;
}

static int mem_mkdir(struct MountsPublicInterface* this_, const char* path, uint32_t mode){
    struct stat st;
    int ret = MEMOUNT_BY_MOUNT(this_)->GetNode( path, &st);
    if ( ret == 0 || (ret == -1&&errno==ENOENT) ){
	ret = MEMOUNT_BY_MOUNT(this_)->Mkdir( path, mode, NULL);
        if ( !ret ) 
            utime_update( NODE_OBJECT_BYINODE( MEMOUNT_BY_MOUNT(this_), st.st_ino ) );
    }
    return ret;
}


static int mem_rmdir(struct MountsPublicInterface* this_, const char* path){
    struct stat st;
    GET_STAT_BYPATH_OR_RAISE_ERROR( MEMOUNT_BY_MOUNT(this_), path, &st);

    const char* name = name_from_path(path);
    ZRT_LOG( L_EXTRA, "name=%s", name );
    if ( name && !strcmp(name, ".\0")  ){
	/*should be specified real path for rmdir*/
	SET_ERRNO(EINVAL);
	return -1;
    }
    return MEMOUNT_BY_MOUNT(this_)->Rmdir( st.st_ino );
}

ssize_t __NON_INSTRUMENT_FUNCTION__ 
mem_read(struct MountsPublicInterface* this_, int fd, void *buf, size_t nbyte);
ssize_t mem_read(struct MountsPublicInterface* this_, int fd, void *buf, size_t nbyte){
    ssize_t ret;
    const struct OpenFileDescription *ofd;
    const struct HandleItem *hentry;
    struct MemNode *mnode = mnode_by_handle_get_ofd_hentry(this_, fd, &ofd, &hentry);
    if (mnode!=NULL){
	ret = this_->pread(this_, fd, buf, nbyte, ofd->offset);
        if ( ret >= 0 ) 
            utime_update( mnode );
        return ret;
    }
    else{
	SET_ERRNO(EBADF);
	return -1;
    }
}

ssize_t __NON_INSTRUMENT_FUNCTION__
mem_write(struct MountsPublicInterface* this_, int fd, const void *buf, size_t nbyte);
ssize_t mem_write(struct MountsPublicInterface* this_, int fd, const void *buf, size_t nbyte){
    ssize_t ret;
    const struct OpenFileDescription *ofd;
    const struct HandleItem *hentry;
    struct MemNode *mnode = mnode_by_handle_get_ofd_hentry(this_, fd, &ofd, &hentry);
    if (mnode!=NULL){
	ret = this_->pwrite(this_, fd, buf, nbyte, ofd->offset);
        if ( ret >= 0 ) 
            utime_update( mnode );
        return ret;
    }
    else{
	SET_ERRNO(EBADF);
	return -1;
    }
}

ssize_t __NON_INSTRUMENT_FUNCTION__ 
mem_pread(struct MountsPublicInterface* this_, int fd, void *buf, size_t nbyte, off_t offset);
ssize_t mem_pread(struct MountsPublicInterface* this_, 
		  int fd, void *buf, size_t nbyte, off_t offset){
    const struct OpenFileDescription *ofd;
    const struct HandleItem *hentry;
    struct MemNode *mnode 
        = mnode_by_handle_get_ofd_hentry(this_, fd, &ofd, &hentry);
    if (mnode!=NULL){
	/*check if file was not opened for reading*/
	CHECK_FILE_OPEN_FLAGS_OR_RAISE_ERROR(ofd->flags&O_ACCMODE, O_RDONLY, O_RDWR);

	ssize_t readed = MEMOUNT_BY_MOUNT(this_)->Read( hentry->inode, offset, buf, nbyte );
	if ( readed >= 0 ){
	    int ret;
            utime_update( mnode );
	    offset += readed;
	    /*update offset*/
	    ret = OFILESPOOL_BY_MOUNT(this_)->set_offset( hentry->open_file_description_id, offset );
	    assert( ret == 0 );
	}
	/*return readed bytes or error*/
	return readed;
    }
    else{
	SET_ERRNO(EBADF);
	return -1;
    }
}

ssize_t __NON_INSTRUMENT_FUNCTION__
mem_pwrite(struct MountsPublicInterface* this_, int fd, const void *buf, size_t nbyte, off_t offset);
ssize_t mem_pwrite(struct MountsPublicInterface* this_, 
                   int fd, const void *buf, size_t nbyte, off_t offset){
    const struct OpenFileDescription *ofd;
    const struct HandleItem *hentry;
    struct MemNode *mnode 
        = mnode_by_handle_get_ofd_hentry(this_, fd, &ofd, &hentry);
    if (mnode!=NULL){
	/*check if file was not opened for writing*/
	CHECK_FILE_OPEN_FLAGS_OR_RAISE_ERROR(ofd->flags&O_ACCMODE, O_WRONLY, O_RDWR);

	ssize_t wrote = MEMOUNT_BY_MOUNT(this_)->Write( hentry->inode, offset, buf, nbyte );
	if ( wrote != -1 ){
	    int ret;
            utime_update( mnode );
	    offset += wrote;
	    /*update offset*/
	    ret = OFILESPOOL_BY_MOUNT(this_)->set_offset( hentry->open_file_description_id, offset );
	    assert( ret == 0 );
	}
	return wrote;
    }
    else{
	SET_ERRNO(EBADF);
	return -1;
    }
}

static int mem_fchown(struct MountsPublicInterface* this_, int fd, uid_t owner, gid_t group){
    const struct OpenFileDescription *ofd;
    const struct HandleItem *hentry;
    struct MemNode *mnode 
        = mnode_by_handle_get_ofd_hentry(this_, fd, &ofd, &hentry);
    if (mnode!=NULL){
        int ret;
	ret = MEMOUNT_BY_MOUNT(this_)->Chown( hentry->inode, owner, group);
        if (!ret)
            utime_update( mnode );
        return ret;
    }
    else{
	SET_ERRNO(EBADF);
	return -1;
    }
}

static int mem_fchmod(struct MountsPublicInterface* this_, int fd, uint32_t mode){
    const struct OpenFileDescription *ofd;
    const struct HandleItem *hentry;
    struct MemNode *mnode 
        = mnode_by_handle_get_ofd_hentry(this_, fd, &ofd, &hentry);
    if (mnode!=NULL){
        int ret;
	ret = MEMOUNT_BY_MOUNT(this_)->Chmod( hentry->inode, mode);
        if (!ret)
            utime_update( mnode );
        return ret;
    }
    else{
	SET_ERRNO(EBADF);
	return -1;
    }
}


static int mem_fstat(struct MountsPublicInterface* this_, int fd, struct stat *buf){
    const struct OpenFileDescription *ofd;
    const struct HandleItem *hentry;
    struct MemNode *mnode 
        = mnode_by_handle_get_ofd_hentry(this_, fd, &ofd, &hentry);
    if (mnode!=NULL){
        int ret;
	ret = MEMOUNT_BY_MOUNT(this_)->Stat( hentry->inode, buf);
        if (!ret)
            utime_update( mnode );
        return  ret;
    }
    else{
	SET_ERRNO(EBADF);
	return -1;
    }
}

static int mem_getdents(struct MountsPublicInterface* this_, int fd, void *buf, unsigned int count){
    const struct OpenFileDescription *ofd;
    const struct HandleItem *hentry;
    struct MemNode *mnode 
        = mnode_by_handle_get_ofd_hentry(this_, fd, &ofd, &hentry);
    if (mnode!=NULL){
	off_t newoffset;
	ssize_t readed = MEMOUNT_BY_MOUNT(this_)->Getdents( hentry->inode, 
							    ofd->offset, &newoffset,
							    (DIRENT*)buf, count);
	if ( readed != -1 ){
	    int ret;
	    ret = OFILESPOOL_BY_MOUNT(this_)->set_offset( hentry->open_file_description_id, 
							  newoffset );
	    assert( ret == 0 );
	}
	return readed;
    }
    else{
	SET_ERRNO(EBADF);
	return -1;
    }
}

static int mem_fsync(struct MountsPublicInterface* this_, int fd){
    errno=ENOSYS;
    return -1;
}

static int mem_close(struct MountsPublicInterface* this_, int fd){
    const struct OpenFileDescription *ofd;
    const struct HandleItem *hentry;
    struct MemNode *mnode 
        = mnode_by_handle_get_ofd_hentry(this_, fd, &ofd, &hentry);
    if (mnode!=NULL){
	int ret;	
        utime_update( mnode );
	MEMOUNT_BY_MOUNT(this_)->Unref(mnode->slot()); /*decrement use count*/
	if ( mnode->UnlinkisTrying() ){
	    ret = MEMOUNT_BY_MOUNT(this_)->UnlinkInternal(mnode);
	    assert( ret == 0 );	
	}
	ret=OFILESPOOL_BY_MOUNT(this_)->release_ofd(hentry->open_file_description_id);    
	assert( ret == 0 );
	ret = HALLOCATOR_BY_MOUNT(this_)->free_handle(fd);
	assert( ret == 0 );
	return 0;
    }
    else{
	SET_ERRNO(EBADF);
	return -1;
    }
}

static off_t mem_lseek(struct MountsPublicInterface* this_, int fd, off_t offset, int whence){
    const struct OpenFileDescription *ofd;
    const struct HandleItem *hentry;
    struct MemNode *mnode 
        = mnode_by_handle_get_ofd_hentry(this_, fd, &ofd, &hentry);
    if (mnode!=NULL){
	if ( !is_dir(this_, hentry->inode) ){
	    off_t next = ofd->offset;
	    int ret;
	    ssize_t len;
	    switch (whence) {
	    case SEEK_SET:
		next = offset;
		break;
	    case SEEK_CUR:
		next += offset;
		break;
	    case SEEK_END:
		len = get_file_len( this_, hentry->inode );
		if (len == -1) {
		    return -1;
		}
		next = static_cast<size_t>(len) + offset;
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
	    ret = OFILESPOOL_BY_MOUNT(this_)->set_offset(hentry->open_file_description_id, next);
	    assert( ret == 0 );
            utime_update( mnode );
	    return next;
	}
    }
    SET_ERRNO(EBADF);
    return -1;
}

static int mem_open(struct MountsPublicInterface* this_, const char* path, int oflag, uint32_t mode){
    int ret=0;
    if (oflag & O_CREAT) {
	/*If need to create path, then do it with errors checking*/
	ret = MEMOUNT_BY_MOUNT(this_)->Open(path, oflag, mode);
    }
    /* get node from memory FS for specified type, if no errors occured 
     * then file allocated in memory FS and require file desc - fd*/
    struct stat st;
    struct stat st_parent;
    if ( ret == 0 && MEMOUNT_BY_MOUNT(this_)->GetNode( path, &st) == 0 ){
	if ( MEMOUNT_BY_MOUNT(this_)->GetParentNode( path, &st_parent) != 0 )
	    st_parent = st;
	int open_file_description_id = OFILESPOOL_BY_MOUNT(this_)->getnew_ofd(oflag);
	/*ask for file descriptor in handle allocator*/
	int fd = HALLOCATOR_BY_MOUNT(this_)->allocate_handle( this_, 
							      st.st_ino,
							      st_parent.st_ino,
							      open_file_description_id);
	if ( fd < 0 ){
	    /*it's hipotetical but possible case if amount of open files 
	      are exceeded an maximum value.*/
	    OFILESPOOL_BY_MOUNT(this_)->release_ofd(open_file_description_id);
	    SET_ERRNO(ENFILE);
	    return -1;
	}
	MEMOUNT_BY_MOUNT(this_)->Ref(st.st_ino); /*set file referred*/
	/*append feature support, is simple*/
	if ( oflag & O_APPEND ){
	    ZRT_LOG(L_SHORT, P_TEXT, "handle flag: O_APPEND");
	    mem_lseek(this_, fd, 0, SEEK_END);
	}

	/*O_TRUNC, O_WRONLY, O_RDWR  for dirs must be rejected*/
	if ( is_dir(this_, st.st_ino) && 
	     (oflag&O_TRUNC || oflag&O_WRONLY || oflag&O_RDWR ) ){
	    SET_ERRNO(EISDIR);
	    return -1;
	}

	/*file truncate support, only for writable files, reset size*/
	if ( oflag&O_TRUNC && (oflag&O_RDWR || oflag&O_WRONLY) ){
	    /*reset file size*/
	    MemNode* mnode = NODE_OBJECT_BYINODE( MEMOUNT_BY_MOUNT(this_), st.st_ino);
	    if (mnode){ 
		ZRT_LOG(L_SHORT, P_TEXT, "handle flag: O_TRUNC");
		/*update stat*/
		st.st_size = 0;
		mnode->set_len(st.st_size);
		ZRT_LOG(L_SHORT, "%s, %d", mnode->name().c_str(), mnode->len() );
	    }
	}
	/*success*/
	return fd;
    }
    SET_ERRNO(ENOENT);
    return -1;
}

static int mem_fcntl(struct MountsPublicInterface* this_, int fd, int cmd, ...){
    const struct OpenFileDescription *ofd;
    const struct HandleItem *hentry;
    struct MemNode *mnode 
        = mnode_by_handle_get_ofd_hentry(this_, fd, &ofd, &hentry);
    if (mnode!=NULL){
	ZRT_LOG(L_INFO, "fcntl cmd=%s", STR_FCNTL_CMD(cmd));
	if ( is_dir(this_, hentry->inode) ){
	    SET_ERRNO(EBADF);
	    return -1;
	}
	return 0;
    }
    else{
	SET_ERRNO(ENOENT);
	return -1;
    }
}

static int mem_remove(struct MountsPublicInterface* this_, const char* path){
    return MEMOUNT_BY_MOUNT(this_)->Unlink(path);
}

static int mem_unlink(struct MountsPublicInterface* this_, const char* path){
    return MEMOUNT_BY_MOUNT(this_)->Unlink(path);
}

/*implementation taken from zerovm/glibc, and moved to here because
  only this filesystem is using it, and another fs has own.*/
static int mem_rename(struct MountsPublicInterface* this_,const char *oldpath, const char *newpath){
    int save = errno;
    if (this_->link(this_ ,oldpath, newpath) < 0){
	if (errno == EEXIST){
	    SET_ERRNO (save);
	    /* Race condition, required for 1003.1 conformance.  */
	    if (this_->unlink(this_, newpath) < 0 ||
		this_->link(this_, oldpath, newpath) < 0)
		return -1;
	}
	else
	    return -1;
    }
    if (this_->unlink(this_, oldpath) < 0){
	save = errno;
	if (this_->unlink(this_, newpath) == 0)
	    SET_ERRNO(save);
	return -1;
    }
    return 0;
}

static int mem_access(struct MountsPublicInterface* this_, const char* path, int amode){
    return -1;
}

static int mem_ftruncate_size(struct MountsPublicInterface* this_, int fd, off_t length){
    const struct OpenFileDescription *ofd;
    const struct HandleItem *hentry;
    struct MemNode *mnode 
        = mnode_by_handle_get_ofd_hentry(this_, fd, &ofd, &hentry);
    if (mnode!=NULL){
	if ( !is_dir(this_, hentry->inode) ){
            int flags = ofd->flags & O_ACCMODE;
            /*check if file was not opened for writing*/
            if ( flags!=O_WRONLY && flags!=O_RDWR ){
                ZRT_LOG(L_ERROR, "file open flags=%s not allow write", 
                        STR_FILE_OPEN_FLAGS(flags));
                SET_ERRNO( EINVAL );
                return -1;
            }
            /*set file length on related node and update new length in stat*/
            mnode->set_len(length);

            /*in according to docs: if doing file size reduce then
              offset should not be changed, but on ubuntu linux
              host offset is not staying beyond of file bounds and
              assignes to max availibale pos. Just do it the same
              as on host */
#define DO_NOT_ALLOW_OFFSET_BEYOND_FILE_BOUNDS_IF_TRUNCATE_REDUCES_FILE_SIZE

#ifdef DO_NOT_ALLOW_OFFSET_BEYOND_FILE_BOUNDS_IF_TRUNCATE_REDUCES_FILE_SIZE
            off_t offset = ofd->offset;
            if ( length < offset ){
                int ret;
                offset = length+1;
                ret = OFILESPOOL_BY_MOUNT(this_)->set_offset(hentry->open_file_description_id,
                                                             offset);
                assert( ret == 0 );
            }
#endif //DO_NOT_ALLOW_OFFSET_BEYOND_FILE_BOUNDS_IF_TRUNCATE_REDUCES_FILE_SIZE

            ZRT_LOG(L_SHORT, "file truncated on %d len, updated st.size=%d",
                    (int)length, get_file_len( this_, hentry->inode ));
            /*file size truncated */
	    return 0;
	}
	else{
	    SET_ERRNO(EBADF);
	    return -1;
	}
    }
    else{
	SET_ERRNO(EBADF);
	return -1;
    }
}

int mem_truncate_size(struct MountsPublicInterface* this_, const char* path, off_t length){
    assert(0); 
    /*truncate implementation replaced by ftruncate call*/
    return -1;
}

static int mem_isatty(struct MountsPublicInterface* this_, int fd){
    return -1;
}

static int mem_dup(struct MountsPublicInterface* this_, int oldfd){
    return -1;
}

static int mem_dup2(struct MountsPublicInterface* this_, int oldfd, int newfd){
    return -1;
}

static int mem_link(struct MountsPublicInterface* this_, const char* oldpath, const char* newpath){
    /*create new hardlink*/
    int ret = MEMOUNT_BY_MOUNT(this_)->Link(oldpath, newpath);
    if ( ret == -1 ){
	/*errno already setted by MemMount*/
	return ret;
    }
    return 0;
}

static int mem_utime(struct MountsPublicInterface *this_,
                     const char *filename, const struct utimbuf *times){

    struct stat st;
    GET_STAT_BYPATH_OR_RAISE_ERROR( MEMOUNT_BY_MOUNT(this_), filename, &st);
    int ret = MEMOUNT_BY_MOUNT(this_)->Stat( st.st_ino, &st);
    if ( ret == 0 ){
	MemNode* node = NODE_OBJECT_BYINODE( MEMOUNT_BY_MOUNT(this_), st.st_ino);
	assert(node!=NULL);
        struct timespec ts_atime;
        struct timespec ts_mtime;
        ts_atime.tv_sec = times->actime;
        ts_atime.tv_nsec = 0;
        ts_mtime.tv_sec = times->modtime;
        ts_mtime.tv_nsec = 0;
        node->set_utime(&ts_atime, &ts_mtime);
    }
    return ret;
}


struct MountSpecificPublicInterface* mem_implem(struct MountsPublicInterface* this_){
    return ((struct InMemoryMounts*)this_)->mount_specific_interface;
}

static struct MountsPublicInterface KInMemoryMountWraper = {
    mem_readlink,
    mem_symlink,
    mem_chown,
    mem_chmod,
    mem_statvfs,
    mem_stat,
    mem_mkdir,
    mem_rmdir,
    mem_read,
    mem_write,
    mem_pread,
    mem_pwrite,
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
    mem_rename,
    mem_access,
    mem_ftruncate_size,
    mem_truncate_size,
    mem_isatty,
    mem_dup,
    mem_dup2,
    mem_link,
    mem_utime,
    mem_implem  /*mount_specific_interface*/
};

struct MountsPublicInterface* 
inmemory_filesystem_construct( struct HandleAllocator* handle_allocator,
			       struct OpenFilesPool* open_files_pool){
    /*use malloc and not new, because it's external c object*/
    struct InMemoryMounts* this_ = (struct InMemoryMounts*)malloc( sizeof(struct InMemoryMounts) );

    /*set functions*/
    this_->public_ = KInMemoryMountWraper;
    /*set data members*/
    this_->handle_allocator = handle_allocator; /*use existing handle allocator*/
    this_->open_files_pool = open_files_pool; /*use existing open files pool*/
    this_->mount_specific_interface = CONSTRUCT_L(MOUNT_SPECIFIC)( &KMountSpecificImplem,
								   this_);
    this_->mem_mount_cpp = new MemMount;
    return (struct MountsPublicInterface*)this_;
}
