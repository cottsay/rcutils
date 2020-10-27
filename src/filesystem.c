// Copyright 2017 Open Source Robotics Foundation, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifdef __cplusplus
extern "C"
{
#endif
#include "rcutils/filesystem.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <dirent.h>
#include <unistd.h>
#else
#include <windows.h>
#include <direct.h>
#endif  // _WIN32

#include "rcutils/error_handling.h"
#include "rcutils/format_string.h"
#include "rcutils/get_env.h"
#include "rcutils/repl_str.h"
#include "rcutils/strdup.h"

#ifdef _WIN32
# define RCUTILS_PATH_DELIMITER "\\"
#else
# define RCUTILS_PATH_DELIMITER "/"
#endif  // _WIN32

typedef struct rcutils_dir_iter_state_t
{
#ifdef _WIN32
  HANDLE handle;
  WIN32_FIND_DATA data;
#else
  DIR * dir;
#endif
} rcutils_dir_iter_state_t;

rcutils_dir_iter_t *
rcutils_dir_iter_start(const char * directory_path, const rcutils_allocator_t allocator)
{
  RCUTILS_CHECK_ARGUMENT_FOR_NULL(directory_path, NULL);
  RCUTILS_CHECK_ALLOCATOR_WITH_MSG(
    &allocator, "allocator is invalid", return NULL);

  rcutils_dir_iter_t * iter = (rcutils_dir_iter_t *)allocator.zero_allocate(
    1, sizeof(rcutils_dir_iter_t), allocator.state);
  if (NULL == iter) {
    return NULL;
  }
  iter->allocator = allocator;

  rcutils_dir_iter_state_t * state = (rcutils_dir_iter_state_t *)allocator.zero_allocate(
    1, sizeof(rcutils_dir_iter_state_t), allocator.state);
  if (NULL == state) {
    goto rcutils_dir_iter_start_fail;
  }
  iter->state = (void *)state;

#ifdef _WIN32
  char * search_path = rcutils_join_path(directory_path, "*", allocator);
  if (NULL == search_path) {
    goto rcutils_dir_iter_start_fail;
  }
  state->handle = FindFirstFile(search_path, &state->data);
  allocator.deallocate(search_path, allocator.state);
  if (INVALID_HANDLE_VALUE == state->handle) {
    DWORD error = GetLastError();
    if (ERROR_FILE_NOT_FOUND != error || !rcutils_is_directory(directory_path)) {
      RCUTILS_SET_ERROR_MSG_WITH_FORMAT_STRING(
        "Can't open directory %s. Error code: %d\n", directory_path, error);
      goto rcutils_dir_iter_start_fail;
    }
  } else {
    iter->entry_name = state->data.cFileName;
  }
#else
  state->dir = opendir(directory_path);
  if (NULL == state->dir) {
    RCUTILS_SET_ERROR_MSG_WITH_FORMAT_STRING(
      "Can't open directory %s. Error code: %d\n", directory_path, errno);
    goto rcutils_dir_iter_start_fail;
  }

  errno = 0;
  struct dirent * entry = readdir(state->dir);
  if (NULL != entry) {
    iter->entry_name = entry->d_name;
  } else if (0 != errno) {
    RCUTILS_SET_ERROR_MSG_WITH_FORMAT_STRING(
      "Can't iterate directory %s. Error code: %d\n", directory_path, errno);
    goto rcutils_dir_iter_start_fail;
  }
#endif

  return iter;

rcutils_dir_iter_start_fail:
  rcutils_dir_iter_end(iter);
  return NULL;
}

bool
rcutils_dir_iter_next(rcutils_dir_iter_t * iter)
{
  RCUTILS_CHECK_ARGUMENT_FOR_NULL(iter, false);
  rcutils_dir_iter_state_t * state = (rcutils_dir_iter_state_t *)iter->state;
  RCUTILS_CHECK_FOR_NULL_WITH_MSG(state, "iter is invalid", false);

#ifdef _WIN32
  if (FindNextFile(state->handle, &state->data)) {
    iter->entry_name = iter->state.data.cFileName;
    return true;
  }
  FindClose(state->handle);
#else
  struct dirent * entry = readdir(state->dir);
  if (NULL != entry) {
    iter->entry_name = entry->d_name;
    return true;
  }
#endif

  iter->entry_name = NULL;
  return false;
}

void
rcutils_dir_iter_end(rcutils_dir_iter_t * iter)
{
  if (NULL == iter) {
    return;
  }

  rcutils_allocator_t allocator = iter->allocator;
  rcutils_dir_iter_state_t * state = (rcutils_dir_iter_state_t *)iter->state;
  if (NULL != state) {
#ifdef _WIN32
    FindClose(state->handle);
#else
    closedir(state->dir);
#endif

    allocator.deallocate(state, allocator.state);
  }

  allocator.deallocate(iter, allocator.state);
}

bool
rcutils_get_cwd(char * buffer, size_t max_length)
{
  if (NULL == buffer || max_length == 0) {
    return false;
  }
#ifdef _WIN32
  if (NULL == _getcwd(buffer, (int)max_length)) {
    return false;
  }
#else
  if (NULL == getcwd(buffer, max_length)) {
    return false;
  }
#endif  // _WIN32
  return true;
}

bool
rcutils_is_directory(const char * abs_path)
{
  struct stat buf;
  if (stat(abs_path, &buf) < 0) {
    return false;
  }
#ifdef _WIN32
  return (buf.st_mode & S_IFDIR) == S_IFDIR;
#else
  return S_ISDIR(buf.st_mode);
#endif  // _WIN32
}

bool
rcutils_is_file(const char * abs_path)
{
  struct stat buf;
  if (stat(abs_path, &buf) < 0) {
    return false;
  }
#ifdef _WIN32
  return (buf.st_mode & S_IFREG) == S_IFREG;
#else
  return S_ISREG(buf.st_mode);
#endif  // _WIN32
}

bool
rcutils_exists(const char * abs_path)
{
  struct stat buf;
  if (stat(abs_path, &buf) < 0) {
    return false;
  }
  return true;
}

bool
rcutils_is_readable(const char * abs_path)
{
  struct stat buf;
  if (stat(abs_path, &buf) < 0) {
    return false;
  }
#ifdef _WIN32
  if (!(buf.st_mode & _S_IREAD)) {
#else
  if (!(buf.st_mode & S_IRUSR)) {
#endif  // _WIN32
    return false;
  }
  return true;
}

bool
rcutils_is_writable(const char * abs_path)
{
  struct stat buf;
  if (stat(abs_path, &buf) < 0) {
    return false;
  }
#ifdef _WIN32
  if (!(buf.st_mode & _S_IWRITE)) {
#else
  if (!(buf.st_mode & S_IWUSR)) {
#endif  // _WIN32
    return false;
  }
  return true;
}

bool
rcutils_is_readable_and_writable(const char * abs_path)
{
  struct stat buf;
  if (stat(abs_path, &buf) < 0) {
    return false;
  }
#ifdef _WIN32
  // NOTE(marguedas) on windows all writable files are readable
  // hence the following check is equivalent to "& _S_IWRITE"
  if (!((buf.st_mode & _S_IWRITE) && (buf.st_mode & _S_IREAD))) {
#else
  if (!((buf.st_mode & S_IWUSR) && (buf.st_mode & S_IRUSR))) {
#endif  // _WIN32
    return false;
  }
  return true;
}

char *
rcutils_join_path(
  const char * left_hand_path,
  const char * right_hand_path,
  rcutils_allocator_t allocator)
{
  if (NULL == left_hand_path) {
    return NULL;
  }
  if (NULL == right_hand_path) {
    return NULL;
  }

  return rcutils_format_string(
    allocator,
    "%s%s%s",
    left_hand_path, RCUTILS_PATH_DELIMITER, right_hand_path);
}

char *
rcutils_to_native_path(
  const char * path,
  rcutils_allocator_t allocator)
{
  if (NULL == path) {
    return NULL;
  }

  return rcutils_repl_str(path, "/", RCUTILS_PATH_DELIMITER, &allocator);
}

char *
rcutils_expand_user(const char * path, rcutils_allocator_t allocator)
{
  if (NULL == path) {
    return NULL;
  }

  if ('~' != path[0]) {
    return rcutils_strdup(path, allocator);
  }

  const char * homedir = rcutils_get_home_dir();
  if (NULL == homedir) {
    return NULL;
  }
  return rcutils_format_string_limit(
    allocator,
    strlen(homedir) + strlen(path),
    "%s%s",
    homedir,
    path + 1);
}

bool
rcutils_mkdir(const char * abs_path)
{
  if (NULL == abs_path) {
    return false;
  }

  if (abs_path[0] == '\0') {
    return false;
  }

  bool success = false;
#ifdef _WIN32
  // TODO(clalancette): Check to ensure that the path is absolute on Windows.
  // In theory we can use PathRelativeA to do this, but I was unable to make
  // it work.  Needs further investigation.

  int ret = _mkdir(abs_path);
#else
  if (abs_path[0] != '/') {
    return false;
  }

  int ret = mkdir(abs_path, 0775);
#endif
  if (ret == 0 || (errno == EEXIST && rcutils_is_directory(abs_path))) {
    success = true;
  }

  return success;
}

size_t
rcutils_calculate_directory_size(const char * directory_path, rcutils_allocator_t allocator)
{
  size_t dir_size = 0;

  if (!rcutils_is_directory(directory_path)) {
    RCUTILS_SAFE_FWRITE_TO_STDERR_WITH_FORMAT_STRING(
      "Path is not a directory: %s\n", directory_path);
    return dir_size;
  }

  rcutils_dir_iter_t * iter = rcutils_dir_iter_start(directory_path, allocator);
  if (NULL == iter) {
    return dir_size;
  }

  do {
    // Skip over local folder handle (`.`) and parent folder (`..`)
    if (strcmp(iter->entry_name, ".") != 0 && strcmp(iter->entry_name, "..") != 0) {
      char * file_path = rcutils_join_path(directory_path, iter->entry_name, allocator);
      dir_size += rcutils_get_file_size(file_path);
      allocator.deallocate(file_path, allocator.state);
    }
  } while (rcutils_dir_iter_next(iter));

  rcutils_dir_iter_end(iter);

  return dir_size;
}

size_t
rcutils_get_file_size(const char * file_path)
{
  if (!rcutils_is_file(file_path)) {
    RCUTILS_SAFE_FWRITE_TO_STDERR_WITH_FORMAT_STRING(
      "Path is not a file: %s\n", file_path);
    return 0;
  }

  struct stat stat_buffer;
  int rc = stat(file_path, &stat_buffer);
  return rc == 0 ? (size_t)(stat_buffer.st_size) : 0;
}

#ifdef __cplusplus
}
#endif
