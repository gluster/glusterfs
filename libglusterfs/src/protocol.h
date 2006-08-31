
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
#define CODE_LEN  9
#define NAME_LEN  33
#define SIZE_LEN  33
#define END_LEN   10

typedef struct {
  int type;
  int code;
  char name[32];
  int size;
  char *data;
} gf_block;

gf_block *gf_block_new (void);
int gf_block_serialize (gf_block *b, char *buf);
int gf_block_serialized_length (gf_block *b);

gf_block *gf_block_unserialize (int fd);
