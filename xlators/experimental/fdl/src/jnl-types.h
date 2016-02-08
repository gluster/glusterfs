#define NEW_REQUEST     (uint8_t)'N'

typedef struct {
        uint8_t         event_type;     /* e.g. NEW_REQUEST */
        uint8_t         fop_type;       /* e.g. GF_FOP_SETATTR */
        uint16_t        request_id;
        uint32_t        ext_length;
} event_header_t;

enum {
        FDL_IPC_BASE = 0xfeedbee5,       /* ... and they make honey */
        FDL_IPC_CHANGE_TERM,
        FDL_IPC_GET_TERMS,
};
