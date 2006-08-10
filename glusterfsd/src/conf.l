
%{

#include "glusterfsd.h"
#include "y.tab.h"
#include <string.h>

%}
CHROOT_     [Cc][Hh][Rr][Oo][Oo][Tt]
SCRATCH_    [Ss][Cc][Rr][Aa][Tt][Cc][Hh]
KEY_LEN     [Kk][Ee][Yy][Ll][Ee][Nn][Gg][Tt][Hh]
LISTEN_PORT [Ll][Ii][Ss][Tt][Ee][Nn][Ii][Nn][Gg][Pp][Oo][Rr][Tt]
DIR_        [Dd][Ii][Rr]
%%
\#.*                  ;
{CHROOT_}{DIR_}       return CHROOT;
{SCRATCH_}{DIR_}      return SCRATCH;
{KEY_LEN}             return KEY_LENGTH;
{LISTEN_PORT}         return PORT;
[0-9]+                { clval = (int)strdup (ctext) ; return NUMBER; }
[a-zA-Z0-9_\./ ]+     { clval = (int)strdup (ctext) ; return DIR_NAME; }
[ \t\n]+              ;
%%
