/*
  (C) 2006 Gluster core team <http://www.gluster.org/>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 

#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

/*
  All value in bytes. '\n' is field seperator.
  Field:<field_length>
  
  ==================
  "Block Start\n":12
  Type:8
  Code:8
  Name:32
  BlockSize:32
  Block:<BlockSize>
  "Block End\n":10
  ==================
*/

#define START_LEN 12
#define TYPE_LEN  9
#define OP_LEN  9
#define NAME_LEN  33
#define SIZE_LEN  33
#define END_LEN   10

typedef struct {
  int type;
  int op;
  char name[32];
  int size;
  char *data;
} gf_block;

gf_block *gf_block_new (void);
int gf_block_serialize (gf_block *b, char *buf);
int gf_block_serialized_length (gf_block *b);

gf_block *gf_block_unserialize (int fd);

#endif
