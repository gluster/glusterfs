
How to introduce new daemons using daemon management framework
==============================================================
Glusterd manages GlusterFS daemons providing services like NFS, Proactive
self-heal, Quota, User servicable snapshots etc. Following are some of the
aspects that come under daemon management.

Data members & functions of different management objects

- **Connection Management**
    - unix domain sockets based channel for internal communication
    - rpc connection for the communication
    - frame timeout value for UDS
    - Methods - notify
    - init, connect, termination, disconnect APIs can be invoked using the
      connection management object

- **Process Management**
    - Name of the process
    - pidfile to detect if the daemon is running
    - loggging directory, log file, volfile, volfileserver & volfileid
    - init, stop APIs can be invoked using the process management object

- **Service Management**
    - connection object
    - process object
    - online status
    - Methods - manager, start, stop which can be abstracted as a common methods
      or specific to service requirements
    - init API can be invoked using the service management object

 The above structures defines the skeleton of the daemon management framework.
 Introduction of new daemons in GlusterFS needs to inherit these properties. Any
 requirement specific to a daemon needs to be implemented in its own service
 (for eg : snapd defines its own type glusterd_snapdsvc_t using glusterd_svc_t
 and snapd specific data). New daemons will need to have its own service specific
 code written in glusterd-<feature>-svc.h{c} and need to reuse the existing
 framework.
