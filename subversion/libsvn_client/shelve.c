/*
 * shelve.c:  implementation of the 'shelve' commands
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

/* ==================================================================== */

/* We define this here to remove any further warnings about the usage of
   experimental functions in this file. */
#define SVN_EXPERIMENTAL

#include "svn_client.h"
#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_hash.h"
#include "svn_utf.h"

#include "client.h"
#include "private/svn_wc_private.h"
#include "svn_private_config.h"


/* Throw an error if NAME does not conform to our naming rules. */
static svn_error_t *
validate_name(const char *name,
              apr_pool_t *scratch_pool)
{
  if (name[0] == '\0' || strchr(name, '/'))
    return svn_error_createf(SVN_ERR_BAD_CHANGELIST_NAME, NULL,
                             _("Shelve: Bad name '%s'"), name);

  return SVN_NO_ERROR;
}

/* Set *PATCH_ABSPATH to the abspath of the patch file for shelved change
 * NAME, no matter whether it exists.
 */
static svn_error_t *
get_patch_abspath(char **patch_abspath,
                  const char *name,
                  const char *wc_root_abspath,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  char *dir;
  const char *filename;

  SVN_ERR(svn_wc__get_shelves_dir(&dir, ctx->wc_ctx, wc_root_abspath,
                                  scratch_pool, scratch_pool));
  filename = apr_pstrcat(scratch_pool, name, ".patch", SVN_VA_NULL);
  *patch_abspath = svn_dirent_join(dir, filename, result_pool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_write_patch(const char *name,
                             const char *message,
                             const char *wc_root_abspath,
                             svn_boolean_t overwrite_existing,
                             const apr_array_header_t *paths,
                             svn_depth_t depth,
                             const apr_array_header_t *changelists,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *scratch_pool)
{
  char *patch_abspath;
  apr_int32_t flag;
  apr_file_t *outfile;
  svn_stream_t *outstream;
  svn_stream_t *errstream;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int i;
  svn_opt_revision_t peg_revision = {svn_opt_revision_unspecified, {0}};
  svn_opt_revision_t start_revision = {svn_opt_revision_base, {0}};
  svn_opt_revision_t end_revision = {svn_opt_revision_working, {0}};

  SVN_ERR(get_patch_abspath(&patch_abspath, name, wc_root_abspath,
                            ctx, scratch_pool, scratch_pool));

  /* Get streams for the output and any error output of the diff. */
  /* ### svn_stream_open_writable() doesn't work here: the buffering
         goes wrong so that diff headers appear after their hunks.
         For now, fix by opening the file without APR_BUFFERED. */
  flag = APR_FOPEN_WRITE | APR_FOPEN_CREATE | APR_FOPEN_TRUNCATE;
  if (! overwrite_existing)
    flag |= APR_FOPEN_EXCL;
  SVN_ERR(svn_io_file_open(&outfile, patch_abspath,
                           flag, APR_FPROT_OS_DEFAULT, scratch_pool));
  outstream = svn_stream_from_aprfile2(outfile, FALSE /*disown*/, scratch_pool);
  SVN_ERR(svn_stream_for_stderr(&errstream, scratch_pool));

  /* Write the patch file header (log message, etc.) */
  if (message)
    {
      SVN_ERR(svn_stream_printf(outstream, scratch_pool, "%s\n",
                                message));
    }
  SVN_ERR(svn_stream_printf(outstream, scratch_pool,
                            "--This line, and those below, will be ignored--\n\n"));
  SVN_ERR(svn_stream_printf(outstream, scratch_pool,
                            "--This patch was generated by 'svn shelve'--\n\n"));

  for (i = 0; i < paths->nelts; i++)
    {
      const char *path = APR_ARRAY_IDX(paths, i, const char *);

      if (svn_path_is_url(path))
        return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                 _("'%s' is not a local path"), path);

      SVN_ERR(svn_client_diff_peg6(
                     NULL /*options*/,
                     path,
                     &peg_revision,
                     &start_revision,
                     &end_revision,
                     NULL,
                     depth,
                     TRUE /*notice_ancestry*/,
                     FALSE /*no_diff_added*/,
                     FALSE /*no_diff_deleted*/,
                     TRUE /*show_copies_as_adds*/,
                     FALSE /*ignore_content_type: FALSE -> omit binary files*/,
                     FALSE /*ignore_properties*/,
                     FALSE /*properties_only*/,
                     FALSE /*use_git_diff_format*/,
                     SVN_APR_LOCALE_CHARSET,
                     outstream,
                     errstream,
                     changelists,
                     ctx, iterpool));
    }
  SVN_ERR(svn_stream_close(outstream));
  SVN_ERR(svn_stream_close(errstream));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_apply_patch(const char *name,
                             const char *wc_root_abspath,
                             svn_boolean_t reverse,
                             svn_boolean_t dry_run,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *scratch_pool)
{
  char *patch_abspath;

  SVN_ERR(get_patch_abspath(&patch_abspath, name, wc_root_abspath,
                            ctx, scratch_pool, scratch_pool));
  SVN_ERR(svn_client_patch(patch_abspath, wc_root_abspath,
                           dry_run, 0 /*strip*/,
                           reverse,
                           FALSE /*ignore_whitespace*/,
                           TRUE /*remove_tempfiles*/, NULL, NULL,
                           ctx, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_delete_patch(const char *name,
                              const char *wc_root_abspath,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *scratch_pool)
{
  char *patch_abspath, *to_abspath;

  SVN_ERR(get_patch_abspath(&patch_abspath, name, wc_root_abspath,
                            ctx, scratch_pool, scratch_pool));
  to_abspath = apr_pstrcat(scratch_pool, patch_abspath, ".bak", SVN_VA_NULL);

  /* remove any previous backup */
  SVN_ERR(svn_io_remove_file2(to_abspath, TRUE /*ignore_enoent*/,
                              scratch_pool));

  /* move the patch to a backup file */
  SVN_ERR(svn_io_file_rename2(patch_abspath, to_abspath, FALSE /*flush_to_disk*/,
                              scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelve(const char *name,
                  const apr_array_header_t *paths,
                  svn_depth_t depth,
                  const apr_array_header_t *changelists,
                  svn_boolean_t keep_local,
                  svn_boolean_t dry_run,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  const char *local_abspath;
  const char *wc_root_abspath;
  const char *message = "";
  svn_error_t *err;

  SVN_ERR(validate_name(name, pool));

  /* ### TODO: check all paths are in same WC; for now use first path */
  SVN_ERR(svn_dirent_get_absolute(&local_abspath,
                                  APR_ARRAY_IDX(paths, 0, char *), pool));
  SVN_ERR(svn_client_get_wc_root(&wc_root_abspath,
                                 local_abspath, ctx, pool, pool));

  /* Fetch the log message and any other revprops */
  if (SVN_CLIENT__HAS_LOG_MSG_FUNC(ctx))
    {
      const char *tmp_file;
      apr_array_header_t *commit_items = apr_array_make(pool, 1, sizeof(void *));

      SVN_ERR(svn_client__get_log_msg(&message, &tmp_file, commit_items,
                                      ctx, pool));
      if (! message)
        return SVN_NO_ERROR;
    }

  err = svn_client_shelf_write_patch(name, message, wc_root_abspath,
                                     FALSE /*overwrite_existing*/,
                                     paths, depth, changelists,
                                     ctx, pool);
  if (err && APR_STATUS_IS_EEXIST(err->apr_err))
    {
      return svn_error_quick_wrapf(err,
                                   "Shelved change '%s' already exists",
                                   name);
    }
  else
    SVN_ERR(err);

  if (!keep_local)
    {
      /* Reverse-apply the patch. This should be a safer way to remove those
         changes from the WC than running a 'revert' operation. */
      SVN_ERR(svn_client_shelf_apply_patch(name, wc_root_abspath,
                                           TRUE /*reverse*/, dry_run,
                                           ctx, pool));
    }

  if (dry_run)
    {
      SVN_ERR(svn_client_shelf_delete_patch(name, wc_root_abspath,
                                            ctx, pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_unshelve(const char *name,
                    const char *local_abspath,
                    svn_boolean_t keep,
                    svn_boolean_t dry_run,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  const char *wc_root_abspath;
  svn_error_t *err;

  SVN_ERR(validate_name(name, pool));

  SVN_ERR(svn_client_get_wc_root(&wc_root_abspath,
                                 local_abspath, ctx, pool, pool));

  /* Apply the patch. */
  err = svn_client_shelf_apply_patch(name, wc_root_abspath,
                                     FALSE /*reverse*/, dry_run,
                                     ctx, pool);
  if (err && err->apr_err == SVN_ERR_ILLEGAL_TARGET)
    {
      return svn_error_quick_wrapf(err,
                                   "Shelved change '%s' not found",
                                   name);
    }
  else
    SVN_ERR(err);

  /* Remove the patch. */
  if (! keep && ! dry_run)
    {
      SVN_ERR(svn_client_shelf_delete_patch(name, wc_root_abspath,
                                            ctx, pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelves_delete(const char *name,
                          const char *local_abspath,
                          svn_boolean_t dry_run,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *pool)
{
  const char *wc_root_abspath;

  SVN_ERR(validate_name(name, pool));

  SVN_ERR(svn_client_get_wc_root(&wc_root_abspath,
                                 local_abspath, ctx, pool, pool));

  /* Remove the patch. */
  if (! dry_run)
    {
      svn_error_t *err;

      err = svn_client_shelf_delete_patch(name, wc_root_abspath,
                                          ctx, pool);
      if (err && APR_STATUS_IS_ENOENT(err->apr_err))
        {
          return svn_error_quick_wrapf(err,
                                       "Shelved change '%s' not found",
                                       name);
        }
      else
        SVN_ERR(err);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_get_paths(apr_hash_t **affected_paths,
                           const char *name,
                           const char *local_abspath,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  const char *wc_root_abspath;
  char *patch_abspath;
  svn_patch_file_t *patch_file;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_t *paths = apr_hash_make(result_pool);

  SVN_ERR(validate_name(name, scratch_pool));

  SVN_ERR(svn_client_get_wc_root(&wc_root_abspath,
                                 local_abspath, ctx, scratch_pool, scratch_pool));
  SVN_ERR(get_patch_abspath(&patch_abspath, name, wc_root_abspath,
                            ctx, scratch_pool, scratch_pool));
  SVN_ERR(svn_diff_open_patch_file(&patch_file, patch_abspath, result_pool));

  while (1)
    {
      svn_patch_t *patch;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_diff_parse_next_patch(&patch, patch_file,
                                        FALSE /*reverse*/,
                                        FALSE /*ignore_whitespace*/,
                                        iterpool, iterpool));
      if (! patch)
        break;
      svn_hash_sets(paths,
                    apr_pstrdup(result_pool, patch->old_filename),
                    apr_pstrdup(result_pool, patch->new_filename));
    }
  SVN_ERR(svn_diff_close_patch_file(patch_file, iterpool));
  svn_pool_destroy(iterpool);

  *affected_paths = paths;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelf_has_changes(svn_boolean_t *has_changes,
                             const char *name,
                             const char *local_abspath,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *scratch_pool)
{
  apr_hash_t *patch_paths;

  SVN_ERR(svn_client_shelf_get_paths(&patch_paths, name, local_abspath,
                                     ctx, scratch_pool, scratch_pool));
  *has_changes = (apr_hash_count(patch_paths) != 0);
  return SVN_NO_ERROR;
}

/* Set *LOGMSG to the log message stored in the file PATCH_ABSPATH.
 *
 * ### Currently just reads the first line.
 */
static svn_error_t *
read_logmsg_from_patch(const char **logmsg,
                       const char *patch_abspath,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  apr_file_t *file;
  svn_stream_t *stream;
  svn_boolean_t eof;
  svn_stringbuf_t *line;

  SVN_ERR(svn_io_file_open(&file, patch_abspath,
                           APR_FOPEN_READ, APR_FPROT_OS_DEFAULT, scratch_pool));
  stream = svn_stream_from_aprfile2(file, FALSE /*disown*/, scratch_pool);
  SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, result_pool));
  SVN_ERR(svn_stream_close(stream));
  *logmsg = line->data;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelves_list(apr_hash_t **shelved_patch_infos,
                        const char *local_abspath,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  char *shelves_dir;
  apr_hash_t *dirents;
  apr_hash_index_t *hi;

  SVN_ERR(svn_wc__get_shelves_dir(&shelves_dir, ctx->wc_ctx, local_abspath,
                                  scratch_pool, scratch_pool));
  SVN_ERR(svn_io_get_dirents3(&dirents, shelves_dir, FALSE /*only_check_type*/,
                              result_pool, scratch_pool));

  *shelved_patch_infos = apr_hash_make(result_pool);

  /* Remove non-shelves */
  for (hi = apr_hash_first(scratch_pool, dirents); hi; hi = apr_hash_next(hi))
    {
      const char *filename = apr_hash_this_key(hi);
      size_t len = strlen(filename);

      if (len > 6 && strcmp(filename + len - 6, ".patch") == 0)
        {
          const char *name = apr_pstrndup(result_pool, filename, len - 6);
          svn_client_shelved_patch_info_t *info
            = apr_palloc(result_pool, sizeof(*info));

          info->dirent = apr_hash_this_val(hi);
          info->mtime = info->dirent->mtime;
          info->patch_path
            = svn_dirent_join(shelves_dir, filename, result_pool);
          SVN_ERR(read_logmsg_from_patch(&info->message, info->patch_path,
                                         result_pool, scratch_pool));

          svn_hash_sets(*shelved_patch_infos, name, info);
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_shelves_any(svn_boolean_t *any_shelved,
                       const char *local_abspath,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *scratch_pool)
{
  apr_hash_t *shelved_patch_infos;

  SVN_ERR(svn_client_shelves_list(&shelved_patch_infos, local_abspath,
                                  ctx, scratch_pool, scratch_pool));
  *any_shelved = apr_hash_count(shelved_patch_infos) != 0;
  return SVN_NO_ERROR;
}
