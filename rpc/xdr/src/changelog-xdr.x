/* XDR: libgfchangelog -> changelog */

struct changelog_probe_req {
       unsigned int filter;
       char sock[UNIX_PATH_MAX];
};

struct changelog_probe_rsp {
       int op_ret;
};

/* XDR: changelog -> libgfchangelog */
struct changelog_event_req {
       /* sequence number for the buffer */
       unsigned long seq;

       /* time of dispatch */
       unsigned long tv_sec;
       unsigned long tv_usec;
};

struct changelog_event_rsp {
       int op_ret;

       /* ack'd buffers sequence number */
       unsigned long seq;
};
