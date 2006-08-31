
%{

#include "glusterfsd.h"
#include "y.tab.h"
#include <string.h>

%}
CHROOT_     [c][h][r][o][o][t]
SCRATCH_    [s][c][r][a][t][c][h]
KEY_LEN     [k][e][y][-][l][e][n][g][t][h]
LISTEN_PORT [l][i][s][t][e][n]
INTERCONNECT_PROTOCOL [i][n][t][e][r][c][o][n][n][e][c][t][-][p][r][o][t][o][c][o][l] 
DIR_        [d][i][r]
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
