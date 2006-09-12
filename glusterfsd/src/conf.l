
%{

#include "glusterfsd.h"
#include "y.tab.h"
#include <string.h>

%}
CHROOT_     chroot
SCRATCH_    scratch
LISTEN_PORT listen
KEY_LEN     key-len
INTERCONNECT_PROTOCOL interconnect-protocol
DIR_        dir
%%
\#.*                  ;
{CHROOT_}[-]{DIR_}       return CHROOT;
{SCRATCH_}[-]{DIR_}      return SCRATCH;
{KEY_LEN}             return KEY_LENGTH;
{LISTEN_PORT}         return PORT;
{INTERCONNECT_PROTOCOL} return PROTOCOL;
[a-zA-Z0-9_\./:\-]+      {cclval = (int)strdup (cctext) ; return ID; }
[ \t\n]+              ;
%%
