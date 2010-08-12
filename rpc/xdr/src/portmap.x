
struct pmap_port_by_brick_req {
       string   brick<>;
};

struct pmap_port_by_brick_rsp {
       int      op_ret;
       int      op_errno;
       int      status;
       int      port;
};


struct pmap_brick_by_port_req {
       int      port;
};

struct pmap_brick_by_port_rsp {
       int      op_ret;
       int      op_errno;
       int      status;
       string   brick<>;
};


struct pmap_signup_req {
};

struct pmap_signup_rsp {
};


struct pmap_signon_req {
};

struct pmap_signon_rsp {
};


struct pmap_signoff_req {
};

struct pmap_signoff_rsp {
};

