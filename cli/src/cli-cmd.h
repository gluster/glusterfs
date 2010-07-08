/*
   Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/

#ifndef __CLI_CMD_H__
#define __CLI_CMD_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "cli.h"

struct cli_cmd {
        const char     *pattern;
        cli_cmd_cbk_t  *cbk;
};

int cli_cmd_volume_register (struct cli_state *state);

int cli_cmd_probe_register (struct cli_state *state);

struct cli_cmd_word *cli_cmd_nextword (struct cli_cmd_word *word,
                                       const char *text);
void cli_cmd_tokens_destroy (char **tokens);

int cli_cmd_await_response ();

int cli_cmd_broadcast_response ();
#endif /* __CLI_CMD_H__ */
