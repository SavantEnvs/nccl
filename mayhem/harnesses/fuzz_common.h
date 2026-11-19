// Shared helpers for the nccl fuzz harnesses (mayhem/ layer, not upstream code).
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// NCCL's file loaders take a path (they fopen() it), so each input goes through a scratch file
// in $TMPDIR (fallback /tmp). One file per process, rewritten per input.
static const char* fuzzScratchPath(const char* suffix) {
  static char path[512];
  if (path[0]) return path;
  const char* dir = getenv("TMPDIR");
  if (dir == NULL || dir[0] == '\0') dir = "/tmp";
  snprintf(path, sizeof(path), "%s/nccl-fuzz-XXXXXX%s", dir, suffix);
  int fd = mkstemps(path, (int)strlen(suffix));
  if (fd < 0) { perror("mkstemps"); abort(); }
  close(fd);
  return path;
}

static void fuzzWriteFile(const char* path, const uint8_t* data, size_t size) {
  FILE* fp = fopen(path, "wb");
  if (fp == NULL) { perror("fopen scratch"); abort(); }
  if (size && fwrite(data, 1, size, fp) != size) { perror("fwrite scratch"); abort(); }
  fclose(fp);
}
