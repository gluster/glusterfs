# GlusterFS RPC program versions

## Compatibility

RPC layer of glusterfs is implemented with possible changes over the protocol layers in mind. If there are any changes in the FOPs from what is assumed to be client side, and whats in serverside, they are to be added as a separate program table.

### Program tables and Versions

A given RPC program has a specific Task, and Version along with actors belonging to the program. In any of the programs, if a new actor is added, it is very important to define one more program with different version, and then keep both, if both are supported. Or else, it is important to handle the 'handshake' properly.

#### Server details

More info on RPC program is at `rpc/rpc-lib/src/rpcsvc.h` and check for structure `rpcsvc_actor_t` and `struct rpcsvc_program`. For usage, check `xlators/protocol/server/src/server-rpc-fops.c`

#### Client details

For details on client structures check `rpc/rpc-lib/src/rpc-clnt.h` for `rpc_clnt_procedure_t` and `rpc_clnt_program_t`. For usage, check `xlators/protocol/client/src/client-rpc-fops.c`

## Protocol

A protocol is what is agreed between two parties. In glusterfs, a RPC protocol is defined as .x file, which then gets converted to .c/.h file using `rpcgen`. There are different protocols defined for communication between `xlators/mgmt/glusterd <==> glusterfsd`, `gluster CLI <==> glusterd`, and `client-protocol <==> server-protocol`

Once a protocol is defined and a release is made with that protocol, make sure no one changes it. Any edits to a given structure there should be a new version of the structure, and also it should get used in new actor, and thus new program version.

## Server and Client Handshake

When a client succeeds to establish a connect (it can be any transport, socket, ib-verbs or unix), client sends a dump (GF_DUMP_DUMP) request to server, which will respond back with all the supported versions of the server RPC (the supported programs which are registered with `rpcsvc_program_register()`).

A client which expects certain programs to be present in server, it should be taking care of looking for it in the handshake methods, and take appropriate action depending on what to do next. In general a compatibility issue should be handled at handshake level itself, thus we can clearly let user/admin know of any 'in-compatibilities'. 
As a developer of GlusterFS protocol layer, one just has to make sure *never to make changes to existing program structures*, but they have to add new programs if required. New programs can have the same actors as present in existing, and also little more. Or it can even have same actor behave differently, take different parameter.

If this is followed properly, there would be smooth upgrade / downgrade of versions. If not, technically, it is 100% guarantee of getting compatibility related issues.
