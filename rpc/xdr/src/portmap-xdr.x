
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
       string brick<>;
       int port;
};

struct pmap_signup_rsp {
       int      op_ret;
       int      op_errno;
};


struct pmap_signin_req {
       string brick<>;
       int port;
};

struct pmap_signin_rsp {
       int      op_ret;
       int      op_errno;
};

struct pmap_signout_req {
       string brick<>;
       int port;
};

struct pmap_signout_rsp {
       int      op_ret;
       int      op_errno;
};
