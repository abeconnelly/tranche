

/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall hello.c `pkg-config fuse --cflags --libs` -o hello
*/

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

static const char *hello_str = "Hello World!\n";
static const char *hello_path = "/hello";

static int hello_getattr(const char *path, struct stat *stbuf)
{
  int res = 0;

  printf("hello_getattr %s\n", path);

  memset(stbuf, 0, sizeof(struct stat));
  if (strcmp(path, "/") == 0) {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
  } else if (strcmp(path, hello_path) == 0) {
    stbuf->st_mode = S_IFREG | 0444;
    stbuf->st_nlink = 1;
    stbuf->st_size = strlen(hello_str);
  } else if (strncmp(path, "/x", 2)==0) {
    stbuf->st_mode = S_IFREG | 0444;
    stbuf->st_nlink = 1;
    stbuf->st_size = 2;
  } else
    res = -ENOENT;

  printf(" >> %d\n", res);

  return res;
}

static int hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
       off_t offset, struct fuse_file_info *fi)
{
  (void) offset;
  (void) fi;

  printf("hello_readdir %s\n", path);

  if (strcmp(path, "/") != 0)
    return -ENOENT;

  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);
  filler(buf, hello_path + 1, NULL, 0);

  return 0;
}

static int hello_open(const char *path, struct fuse_file_info *fi)
{

  printf("hello_open %s\n", path);

  if (strncmp(path, "/x", 2)==0) {

    printf(" ?? %i\n", fi->flags & 3 );

    if ((fi->flags & 3) != O_RDONLY)
      return -EACCES;

    printf(" >> ok\n");

    return 0;
  }

  if (strcmp(path, hello_path) != 0)
    return -ENOENT;

  if ((fi->flags & 3) != O_RDONLY)
    return -EACCES;

  return 0;
}

static int hello_read(const char *path, char *buf, size_t size, off_t offset,
          struct fuse_file_info *fi)
{
  static char *ok = "ok";
  size_t len;
  (void) fi;

  printf("hello_read %s\n", path);

  if (strncmp(path, "/x", 2)==0) {
    if ((offset+size)>2) {
      size = 2-offset;
    }
    if (size<=0) { return 0; }

    memcpy(buf, ok+offset, size);
    return size;
  }

  if(strcmp(path, hello_path) != 0)
    return -ENOENT;

  len = strlen(hello_str);
  if (offset < len) {
    if (offset + size > len)
      size = len - offset;
    memcpy(buf, hello_str + offset, size);
  } else
    size = 0;

  return size;
}

static struct fuse_operations hello_oper = {
  .getattr  = hello_getattr,
  .readdir  = hello_readdir,
  .open   = hello_open,
  .read   = hello_read,
};

int main(int argc, char *argv[])
{
  return fuse_main(argc, argv, &hello_oper, NULL);
}
