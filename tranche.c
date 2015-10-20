/*
  tranche:
    expose seekable slice of bytes in a file through a FUSE mount.

  Copyright (C) 2015 Curoverse, Inc. (info@curoverse.com)

  This program can be distributed under the terms of the GNU AGPLv3.
  See the file COPYING.

  gcc -Wall tranche.c `pkg-config fuse --cflags --libs` -o tranche

  Based on "FUSE: Filesystem in Userspace" exmaple file 'hello.c'
  by Miklos Szeredi <miklos@szeredi.hu>
  Original 'hello.c' Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  and distributed under the terms of the GNU GPL.

  Basic usage:

      ./tranche -b 10 -s 10 -f exmple/hello.txt -m example/mnt -p

    then, in another window

      cat examle/mnt/1:5

    should give the (10+1) to (10+1+5) bytes of the file 'hello.txt'


*/

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include <time.h>
#include <utime.h>

char *VERSION_STR = "0.1.0";

int tranche_start;
int tranche_size;
char *tranche_mountpoint;
char *tranche_exposed_fn;
char *tranche_ifn;

int tranche_ifd;
struct stat tranche_stbuf;

int help_flag=0, quiet_flag=0;
//int quiet_flag;
FILE *tranche_ofp;

char *debug_ofn = "/tmp/tranche.log";
char *tranche_mount_path;

int g_debug=0;

static int parse_range(const char *range, long int *beg, long int *len) {
  int i, n, p_count=0, c_count=0, n_count=0, pos=-1;
  char *s = strdup(range);
  long int t;

  n = strlen(s);

  *beg = 0;
  *len = -1;

  for (i=0; i<n; i++) {
    if (s[i]=='+') { p_count++; pos=i;}
    else if (s[i]==':') { c_count++; pos=i; }
    else if (s[i]=='-') { n_count++; pos=i; }
    else if (s[i]<'0') { free(s); return -1; }
    else if (s[i]>'9') { free(s); return -2; }
  }
  if ((p_count+c_count+n_count)>1) { free(s); return -5; }

  if (pos<0) {
    *beg = strtol(s, NULL, 0);
    if (((*beg)==LONG_MIN) || ((*beg)==LONG_MAX))  {
      if (errno==ERANGE) { free(s); *beg=0; return -6; }
    }
    return 0;
  }

  s[pos] ='\0';
  *beg = strtol(s, NULL, 0);
  if (((*beg)==LONG_MIN) || ((*beg)==LONG_MAX))  {
    if (errno==ERANGE) { free(s); *beg=0; return -7; }
  }

  if ((pos+1)==n) {
    t = -1;
  } else {
    t = strtol(s+pos+1, NULL, 0);
    if (((t)==LONG_MIN) || ((t)==LONG_MAX))  {
      if (errno==ERANGE) { free(s); *beg=0; return -8; }
    }
  }

  if (t<0) { *len = t; }
  else {
    if (p_count) { *len = t; }
    else if (c_count) { *len = t-*beg; }
    else if (n_count) { *len = t-*beg; }
    else { *len=-1; }
  }

  free(s);

  if (*len<-1) { return -9; }
  if (*len==0) { return -10; }

  return 0;
}



static void *tranche_init(struct fuse_conn_info *conn) {

  // Print out the exposed file name we'll be using
  //
  if(!quiet_flag) {
    fprintf(tranche_ofp, "%s\n", tranche_exposed_fn);
    fflush(tranche_ofp);
  }

  return NULL;
}

static int tranche_getattr(const char *path, struct stat *stbuf)
{
  long int st, len;
  int r = 0;
  int res = 0;

  if (tranche_ifd<0) { return -ENOENT; }

  if (g_debug) { printf(">>>> tranche_getattr %s\n", path); }

  if (strcmp(path, "/kill")==0) {
    stbuf->st_mode = S_IFREG | 0777;
    stbuf->st_nlink = 1;
    stbuf->st_size = 0;
    return 0;
  }


  memset(stbuf, 0, sizeof(struct stat));
  if (strcmp(path, "/") == 0) {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
    return res;
  }

  if ((r=parse_range(path+1, &st, &len))<0) {

    if (strcmp(path, tranche_mount_path) == 0) {
      stbuf->st_mode = S_IFREG | 0444;
      stbuf->st_nlink = 1;

      if ((tranche_start+tranche_size) < tranche_stbuf.st_size) {
        stbuf->st_size = (ssize_t)tranche_size;
      } else {
        stbuf->st_size = tranche_stbuf.st_size - tranche_start;
      }
    } else
      res = -ENOENT;

    return res;
  }

  stbuf->st_mode = S_IFREG | 0444;
  stbuf->st_nlink = 1;

  if (len<0) { len = tranche_size; }
  if ((st+len) > (tranche_start+tranche_size)) {
    len = tranche_start+tranche_size - st;
    if (len<0) { len=0; }
  }

  stbuf->st_size = (ssize_t)len;

  if (g_debug) {
    printf("??? %d\n", (int)len);
  }

  return res;
}

static int tranche_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
       off_t offset, struct fuse_file_info *fi)
{
  (void) offset;
  (void) fi;

  if (g_debug) { printf(">>>> tranche_readdir %s\n", path); }

  if (strcmp(path, "/") != 0)
    return -ENOENT;

  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);
  filler(buf, tranche_mount_path + 1, NULL, 0);

  return 0;
}

static int tranche_open(const char *path, struct fuse_file_info *fi)
{
  long int st, len;
  int r=0;

  if (g_debug) { printf(">>>> tranche_open %s\n", path); }
  if (strcmp(path, "/kill")==0) { return 0; }

  if ((r=parse_range(path+1, &st, &len))<0) {

    if (strcmp(path, tranche_mount_path) != 0)
      return -ENOENT;

    if ((fi->flags & 3) != O_RDONLY)
      return -EACCES;

    return 0;
  }

  if ((fi->flags & 3) != O_RDONLY)
    return -EACCES;

  //tranche_ifd = open(tranche_ifn, O_RDONLY);
  //if (tranche_ifd < 0) { return errno; }

  return 0;
}

static int tranche_release(const char *path, struct fuse_file_info *fi)
{
  int z;
  size_t n;
  char *umount_str;

  if (g_debug) { printf(">>>> tranche_release %s\n", path); }
  if (strcmp(path, "/kill")==0) {
    n = strlen(tranche_mountpoint) + strlen("fusermount -u -z ") + 1;
    umount_str = (char *)malloc(sizeof(char)*n);
    snprintf(umount_str, n, "fusermount -u -z %s", tranche_mountpoint);
    z = system(umount_str);
    free(umount_str);
    if (z<0) { return z; }
    return 0;
  }

  if (strcmp(path, tranche_mount_path) != 0)
    return -ENOENT;

  if ((fi->flags & 3) != O_RDONLY)
    return -EACCES;

  return 0;
}

static int tranche_read(const char *path, char *buf, size_t size, off_t offset,
          struct fuse_file_info *fi)
{
  long int st, len;
  size_t start_actual=0, size_actual=0;
  int r=0;
  (void) fi;

  if (g_debug) { printf(">>>> tranche_read %s\n", path); }

  if (strcmp(path, "/kill")==0) { return 0; }

  if ((r=parse_range(path+1, &st, &len))<0) {
    if(strcmp(path, tranche_mount_path) != 0)
      return -ENOENT;

    if (offset >= tranche_size) { return -EFAULT; }
    if ((tranche_start + offset) > tranche_stbuf.st_size) { return -EFAULT; }
    if ((offset+size) > tranche_size) { size = tranche_size - offset; }
    if (size==0) { return -EFAULT; }

    return pread(tranche_ifd, buf, size, tranche_start + offset);
  }

  if (g_debug) {
    printf("!!!!! tranche_start %d, offset %d, st %d, size %d, len %d\n",
        (int)tranche_start, (int)offset, (int)st, (int)size, (int)len);
  }

  if (len<0) { len = tranche_size; }

  start_actual = tranche_start + offset + st;
  size_actual = len;
  if (size_actual > size) { size_actual = size; }
  if ((start_actual + size_actual) > (tranche_start + tranche_size)) {
    size_actual = tranche_start + tranche_size - start_actual;
    if (size_actual<0) { size_actual=0; }
  }

  if (g_debug) {
    printf("(A) tranche_start %d, tranche_size %d, offset %d, st %d, size %d, len %d\n",
        (int)tranche_start, (int)tranche_size, (int)offset, (int)st, (int)size, (int)len);

    printf("(A) size_actual %d, start_actual %d\n", (int)size_actual, (int)start_actual);
  }


  if ((start_actual + size_actual) > (tranche_start + tranche_size)) { return -EFAULT; }
  if (start_actual < tranche_start) { return -EFAULT; }
  if (size_actual==0) { return -EFAULT; }

  if (g_debug) {
    printf("(B) size_actual %d, start_actual %d\n", (int)size_actual, (int)start_actual);
  }

  return pread(tranche_ifd, buf, size_actual, start_actual);
}

static int tranche_utimens(const char *path, const struct timespec tv[2]) {
  return 0;
}

static struct fuse_operations tranche_oper = {
  .init     = tranche_init,
  .getattr  = tranche_getattr,
  .readdir  = tranche_readdir,
  .open     = tranche_open,
  .release  = tranche_release,
  .read     = tranche_read,
  .utimens  = tranche_utimens,
};

void show_help() {
  fprintf(stderr, "Tranche %s, expose bytes from an underlying file through a FUSE mount.\n", VERSION_STR);
  fprintf(stderr, "Usage: tranche [-b begin] [-s size] -f file [-o outfile] [-m mountpoint] [-q] [-p] [-D] [-h]\n");
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  -b begin       start byte position to expose file\n");
  fprintf(stderr, "  -s size        size in bytes to expose\n");
  fprintf(stderr, "  -f file        input file to expose\n");
  fprintf(stderr, "  -o outfile     explicit output file name (default is random file in mountpoint)\n");
  fprintf(stderr, "  -m mountpoint  explicit mount point (default is random directory in /tmp)\n");
  fprintf(stderr, "  -q             quiet. Do not print out exposed outfile\n");
  fprintf(stderr, "  -p             persist. Do not go into background\n");
  fprintf(stderr, "  -D             debug output\n");
  fprintf(stderr, "  -h             help (this screen)\n");
}

int main(int argc, char *argv[])
{
  int i,n;
  int opt, r=0, fuse_ret=0;
  int tmp_mntpoint=0;

  //FILE *tranche_ofp = stdout;

  char **fuse_args;
  int persist_flag = 0;

  tranche_ofp = stdout;

  fuse_args = (char **)malloc(sizeof(char*)*3);
  fuse_args[0] = strdup(argv[0]);
  fuse_args[1] = NULL;
  fuse_args[2] = strdup("-f");

  tranche_start = 0;
  tranche_size = -1;
  tranche_mountpoint=NULL;
  tranche_exposed_fn=NULL;
  tranche_ifn=NULL;
  tranche_ifd=-1;

  memset(&tranche_stbuf, 0, sizeof(struct stat));

  while ((opt=getopt(argc, argv, "b:s:f:m:qhpD"))!=-1) switch(opt) {
    case 'b':
      tranche_start = atoi(optarg);
      break;
    case 's':
      tranche_size = atoi(optarg);
      break;
    case 'f':
      tranche_ifn = strdup(optarg);
      break;
    case 'm':
      tranche_mountpoint = strdup(optarg);
      break;
    case 'q':
      quiet_flag = 1;
      break;
    case 'h':
      help_flag = 1;
      break;
    case 'p':
      persist_flag = 1;
      break;
    case 'D':
      g_debug = 1;
      break;
    default:
      break;
  }

  if (help_flag) {
    show_help();
    exit(1);
  }

  if (!tranche_ifn) {
    fprintf(stderr, "Provide input filename (-f)\n");
    show_help();
    exit(1);
  }

  // Create mount point and construct exposed filename
  //
  if (!tranche_mountpoint) {
    tranche_mountpoint = strdup("/tmp/tranchemntXXXXXX");
    mkdtemp(tranche_mountpoint);
    tmp_mntpoint=1;
  }
  tranche_exposed_fn = tempnam(tranche_mountpoint, NULL);
  fuse_args[1] = strdup(tranche_mountpoint);

  // Open the underlying file and get stats for it
  //
  tranche_ifd = open(tranche_ifn, O_RDONLY);
  if (tranche_ifd<0) {
    perror("error: ");
    exit(errno);
  }



  r = fstat(tranche_ifd, &tranche_stbuf);
  if (r<0) {
    perror("error:");
    return r;
  }

  if ((tranche_size<0) || ((tranche_stbuf.st_size-tranche_start) < tranche_size)) {
    tranche_size = tranche_stbuf.st_size-tranche_start;
  }
  if (tranche_size<=0) { tranche_size=0; }

  n = strlen(tranche_exposed_fn);
  for (i=n-1; i>=0; i--) {
    if (tranche_exposed_fn[i]=='/') { break; }
  }
  if (i<0) { i=0; }
  tranche_mount_path = strdup(tranche_exposed_fn+i);

  // Start fuse server.
  //
  // The fuse server is running in the foreground (the '-f'
  // option in fuse_args) and will block until it's done.
  //
  fuse_ret = fuse_main(2+persist_flag, fuse_args, &tranche_oper, NULL);

  // Fuse serverhas been stopped, close our underlying file
  //
  close(tranche_ifd);

  // Cleanup the mount point
  //
  if (tmp_mntpoint) {
    r = rmdir(tranche_mountpoint);
    if (r<0) { perror(""); }
  }

  free(fuse_args[0]);
  free(fuse_args[1]);
  free(fuse_args[2]);
  free(fuse_args);

  free(tranche_ifn);
  free(tranche_mountpoint);

  return fuse_ret;
}
