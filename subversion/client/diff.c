/*
 * diff.c:  the command-line's portion of the "svn diff" command
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

/* ==================================================================== */



/*** Includes. ***/
#include <apr_hash.h>
#include <apr_tables.h>
#include <apr_portable.h>
#include <unistd.h>
#include "svn_hash.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "cl.h"
#include "config.h"  /* for SVN_CLIENT_DIFF */


svn_error_t *
svn_cl__print_file_diff (svn_string_t *path,
                         apr_pool_t *pool)
{
  apr_status_t status;
  svn_error_t *err;
  svn_string_t *pristine_copy_path;
  const char *args[5];
  apr_os_file_t stdout_fileno = STDOUT_FILENO;

  apr_file_t *outhandle = NULL;

  /* We already have a path to the working version of our file, that's
     PATH. */

  /* Get a PRISTINE_COPY_PATH to compare against.  */
  err = svn_client_file_diff (path, &pristine_copy_path, pool);
  if (err) return err;

  /* Get an apr_file_t representing stdout, which is where we'll have
     the diff program print to. */
  status = apr_put_os_file (&outhandle, &stdout_fileno, pool);
  if (status)
    return svn_error_create (status, 0, NULL, pool,
                             "error: can't open handle to stdout");

  /* Execute local diff command on these two paths, print to stdout. */

  args[0] = SVN_CLIENT_DIFF;  /* the autoconfiscated system diff program */
  args[1] = "-c";
  args[2] = path->data;
  args[3] = pristine_copy_path->data;
  args[4] = NULL;

  err = svn_wc_run_cmd_in_directory (svn_string_create (".", pool), 
                                     SVN_CLIENT_DIFF,
                                     args,
                                     NULL, outhandle, NULL, pool);
  if (err) return err;
  
  apr_close (outhandle);

  /* Cleanup the tmp pristine file now that we're done with it. */
  status = apr_remove_file (pristine_copy_path->data, pool);
  if (status)
    return svn_error_createf (status, 0, NULL, pool,
                              "error: can't remove temporary file `%s'",
                              pristine_copy_path->data);

  return SVN_NO_ERROR;
}




/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */



