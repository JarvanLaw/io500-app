/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */
/******************************************************************************\
*                                                                              *
*        Copyright (c) 2003, The Regents of the University of California       *
*      See the file COPYRIGHT for a complete copyright notice and license.     *
*                                                                              *
********************************************************************************
*
* Implement of abstract I/O interface for GRAMFS.
*
\******************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>

#ifdef __linux__
#  include <sys/ioctl.h>          /* necessary for: */
#  define __USE_GNU               /* O_DIRECT and */
#  include <fcntl.h>              /* IO operations */
#  undef __USE_GNU
#endif                          /* __linux__ */

#include <errno.h>
#include <fcntl.h>              /* IO operations */
#include <sys/stat.h>
#include <assert.h>

#include "ior.h"
#include "aiori.h"
#include "iordef.h"
#include "utilities.h"

#include <sys/statvfs.h>
#include <gramfs/gramfs.h>

#ifndef   O_BINARY              /* Required on Windows    */
#  define O_BINARY 0
#endif

/**************************** P R O T O T Y P E S *****************************/
static void *GRAMFS_Create(char *, IOR_param_t *);
static int GRAMFS_Mknod(char *testFileName);
static void *GRAMFS_Open(char *, IOR_param_t *);
static IOR_offset_t GRAMFS_Xfer(int, void *, IOR_size_t *,
                           IOR_offset_t, IOR_param_t *);
static void GRAMFS_Close(void *, IOR_param_t *);
static void GRAMFS_Delete(char *, IOR_param_t *);
static char* GRAMFS_GetVersion();
static void GRAMFS_Fsync(void *, IOR_param_t *);
static IOR_offset_t GRAMFS_GetFileSize(IOR_param_t *, MPI_Comm, char *);
static int GRAMFS_Access(const char *, int, IOR_param_t *);
static int GRAMFS_Statfs(const char *, ior_aiori_statfs_t *, IOR_param_t * param);
static int GRAMFS_Mkdir(const char *path, mode_t mode, IOR_param_t * param);
static int GRAMFS_Rmdir(const char *path, IOR_param_t * param);
static int GRAMFS_Access(const char *path, int mode, IOR_param_t * param);
static int GRAMFS_Stat(const char *path, struct stat *buf, IOR_param_t * param);
static void GRAMFS_Initialize();
static void GRAMFS_Finalize();
static char* GRAMFS_GetVersion();
static option_help * GRAMFS_options();
/************************** O P T I O N S *****************************/

struct gramfs_options {
  //char * user;
  char * conf;
  char * prefix;
};

static struct gramfs_options o = {
  //.user = NULL,
  .conf = NULL,
  .prefix = NULL,
};

static option_help options [] = {
     // {0, "gramfs.user", "Username for the ceph cluster", OPTION_REQUIRED_ARGUMENT, 's', & o.user},
      {0, "gramfs.conf", "Config file for the gramfs client", OPTION_REQUIRED_ARGUMENT, 's', & o.conf},
      {0, "gramfs.prefix", "mount prefix",  OPTION_REQUIRED_ARGUMENT, 's', & o.prefix},
      LAST_OPTION
};

option_help * GRAMFS_options(void ** init_backend_options, void * init_values){
	return options;
}

static char* GRAMFS_GetVersion() {
	static char ver[1024] = {};

    sprintf(ver, "%s", "GRAMFS-V1.0");
    return ver;
}

/************************** D E C L A R A T I O N S ***************************/
ior_aiori_t gramfs_aiori = {
        .name = "GRAMFS",
        .name_legacy = NULL,
        .create = GRAMFS_Create,
        .mknod = GRAMFS_Mknod,
        .open = GRAMFS_Open,
        .xfer = GRAMFS_Xfer,
        .close = GRAMFS_Close,
        .delete = GRAMFS_Delete,
        .get_version = GRAMFS_GetVersion,
        //.fsync = GRAMFS_Fsync,
        .get_file_size = GRAMFS_GetFileSize,
        .statfs = GRAMFS_Statfs,
        .mkdir = GRAMFS_Mkdir,
        .rmdir = GRAMFS_Rmdir,
        .access = GRAMFS_Access,
        .stat = GRAMFS_Stat,
		.initialize = GRAMFS_Initialize,
		.finalize   = GRAMFS_Finalize,
        .get_options = GRAMFS_options,
        .enable_mdtest = true,
        //.sync = GRAMFS_Sync
};

/***************************** F U N C T I O N S ******************************/

static void GRAMFS_Initialize()
{
	return gramfs_initialize();
}

static void GRAMFS_Finalize()
{
	return gramfs_finalize();
}
/*
 * Creat and open a file through the GRAMFS interface.
 */
static void *GRAMFS_Create(char *testFileName, IOR_param_t * param)
{
        int fd_oflag = O_BINARY;
        int mode = 0664;

        void *fd = NULL;

        if(param->dryRun)
          return 0;

		fd_oflag |= O_CREAT | O_RDWR;

		fd = gramfs_open(testFileName, fd_oflag, mode);
		if (!fd)
				ERRF("gramfs open(\"%s\", %d, %#o) failed",
						testFileName, fd_oflag, mode);

        return fd;
}

/*
 * Creat a file through mknod interface.
 */
static int GRAMFS_Mknod(char *testFileName)
{
    void* fd;
    fd = gramfs_create(testFileName, S_IFREG | S_IRUSR);
    if (!fd) {
        ERR("mknod failed");
        return -1;
    }
    return 0;
}

/*
 * Open a file through the GRAMFS interface.
 */
static void *GRAMFS_Open(char *testFileName, IOR_param_t * param)
{
	int fd_oflag = O_BINARY;
	void *fd;

	fd_oflag |= O_RDWR;

	if(param->dryRun)
	  return NULL;

	fd = gramfs_open(testFileName, fd_oflag, param->mode);
	if(!fd)
		ERRF("gramfs_open(\"%s\", %d) failed", testFileName, fd_oflag);

	return fd;
}

/*
 * Write or read access to file using the GRAMFS interface.
 */
static IOR_offset_t GRAMFS_Xfer(int access, void *file, IOR_size_t * buffer,
                               IOR_offset_t length, IOR_param_t * param)
{

	int64_t remaining = (int64_t)length;

	char *ptr = (char *)buffer;
	int64_t rc;

   if(param->dryRun)
		  return length;
	while(remaining > 0) {
		if(access == WRITE) {
			rc = gramfs_write(file, ptr, remaining, (int64_t)param->offset);
			if (rc == -1)
				ERRF("write(%p, %lld) failed", (void*)ptr, remaining);
            //if (param->fsyncPerWrite == TRUE)
            //        Gramfs_Fsync(&fd, param);
		} else {
			rc = gramfs_read(file, ptr, remaining, (int64_t)param->offset);
			if (rc == 0)
				ERRF("read(%p, %lld) returned EOF prematurely", (void*)ptr, remaining);
			if (rc == -1)
				ERRF("read(%p, %lld) failed", (void*)ptr, remaining);
		}
        remaining -= rc;
        ptr += rc;
	}

	return (length);
/*
	int xferRetries = 0;
	if (lseek64(fd, param->offset, SEEK_SET) == -1)
			ERRF("lseek64(%d, %lld, SEEK_SET) failed", fd, param->offset);

	while (remaining > 0) {

			if (rc < remaining) {
					fprintf(stdout,
							"WARNING: Task %d, partial %s, %lld of %lld bytes at offset %lld\n",
							rank,
							access == WRITE ? "write()" : "read()",
							rc, remaining,
							param->offset + length - remaining);
					if (param->singleXferAttempt == TRUE)
							MPI_CHECK(MPI_Abort(MPI_COMM_WORLD, -1),
									  "barrier error");
					if (xferRetries > MAX_RETRY)
							ERR("too many retries -- aborting");
			}
			assert(rc >= 0);
			assert(rc <= remaining);
			remaining -= rc;
			ptr += rc;
			xferRetries++;
	}
	return (length);*/
}

/*
 * Perform fsync().
 */
static void GRAMFS_Fsync(void *fd, IOR_param_t * param)
{
       // if (fsync(*(int *)fd) != 0)
       //         EWARNF("fsync(%d) failed", *(int *)fd);
}

static void GRAMFS_Sync(IOR_param_t * param)
{
	/*int ret = system("sync");
	if (ret != 0){
		FAIL("Error executing the sync command, ensure it exists.");
	}*/
}


/*
 * Close a file through the GRAMFS interface.
 */
static void GRAMFS_Close(void *fd, IOR_param_t *param)
{
        if(param->dryRun)
          return;
        gramfs_close(fd);
}

/*
 * Delete a file through the GRAMFS interface.
 */
static void GRAMFS_Delete(char *testFileName, IOR_param_t * param)
{
	if(param->dryRun)
		return;
	if(gramfs_delete(testFileName)) {
		EWARNF("[RANK %03d]: unlink() of file \"%s\" failed\n",
				   rank, testFileName);
	}
}

static int GRAMFS_Statfs(const char *path, ior_aiori_statfs_t *stat_buf, IOR_param_t *param) {
	int ret;
	struct statvfs statfs_buf;
	ret = gramfs_statfs(path, &statfs_buf);

    stat_buf->f_bsize = statfs_buf.f_bsize;
    stat_buf->f_blocks = statfs_buf.f_blocks;
    stat_buf->f_bfree = statfs_buf.f_bfree;
    stat_buf->f_files = statfs_buf.f_files;
    stat_buf->f_ffree = statfs_buf.f_ffree;
	//stat_buf->f_bavail = statfs_buf.f_bavail;

	return ret;
}

static int GRAMFS_Mkdir(const char *path, mode_t mode, IOR_param_t * param) {
	int ret;

	ret = gramfs_mkdir(path, mode);

	return ret;
}

static int GRAMFS_Rmdir(const char *path, IOR_param_t * param) {
	int ret;

	ret = gramfs_rmdir(path);

	return ret;
}

static int GRAMFS_Access(const char *path, int mode, IOR_param_t * param) {
	int ret;

	ret = gramfs_access(path, mode);

	return ret;
}

static int GRAMFS_Stat(const char *path, struct stat *buf, IOR_param_t * param) {
	int ret;

	ret = gramfs_stat(path, buf);

	return ret;
}

/*
 * Use Gramfs stat() to return aggregate file size.
 */
static IOR_offset_t GRAMFS_GetFileSize(IOR_param_t * test, MPI_Comm testComm,
                                      char *testFileName)
{
        if(test->dryRun)
          return 0;
        struct stat stat_buf;
        IOR_offset_t aggFileSizeFromStat;

        if(gramfs_stat(testFileName, &stat_buf)) {
        	ERRF("stat(\"%s\", ...) failed", testFileName);
        }
        aggFileSizeFromStat = stat_buf.st_size;

        return (aggFileSizeFromStat);

/*      IOR_offset_t aggFileSizeFromStat, tmpMin, tmpMax, tmpSum;

        if (stat(testFileName, &stat_buf) != 0) {
                ERRF("stat(\"%s\", ...) failed", testFileName);
        }
        aggFileSizeFromStat = stat_buf.st_size;

        if (test->filePerProc == TRUE) {
                MPI_CHECK(MPI_Allreduce(&aggFileSizeFromStat, &tmpSum, 1,
                                        MPI_LONG_LONG_INT, MPI_SUM, testComm),
                          "cannot total data moved");
                aggFileSizeFromStat = tmpSum;
        } else {
                MPI_CHECK(MPI_Allreduce(&aggFileSizeFromStat, &tmpMin, 1,
                                        MPI_LONG_LONG_INT, MPI_MIN, testComm),
                          "cannot total data moved");
                MPI_CHECK(MPI_Allreduce(&aggFileSizeFromStat, &tmpMax, 1,
                                        MPI_LONG_LONG_INT, MPI_MAX, testComm),
                          "cannot total data moved");
                if (tmpMin != tmpMax) {
                        if (rank == 0) {
                                WARN("inconsistent file size by different tasks");
                        }
                         incorrect, but now consistent across tasks
                        aggFileSizeFromStat = tmpMin;
                }
        }
        return (aggFileSizeFromStat);
        */
}
