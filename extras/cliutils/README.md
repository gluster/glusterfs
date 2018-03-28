# CLI utility for creating Cluster aware CLI tools for Gluster
cliutils is a Python library which provides wrapper around `gluster system::
execute` command to extend the functionalities of Gluster.

Example use cases:
- Start a service in all peer nodes of Cluster
- Collect the status of a service from all peer nodes
- Collect the config values from each peer nodes and display latest
  config based on version.
- Copy a file present in GLUSTERD_WORKDIR from one peer node to all
  other peer nodes.(Geo-replication create push-pem is using this to
  distribute the SSH public keys from all master nodes to all slave
  nodes)
- Generate pem keys in all peer nodes and collect all the public keys
  to one place(Geo-replication gsec_create is doing this)
- Provide Config sync CLIs for new features like `gluster-eventsapi`,
  `gluster-restapi`, `gluster-mountbroker` etc.

## Introduction

If a executable file present in `$GLUSTER_LIBEXEC` directory in all
peer nodes(Filename startswith `peer_`) then it can be executed by
running `gluster system:: execute` command from any one peer node.

- This command will not copy any executables to peer nodes, Script
  should exist in all peer nodes to use this infrastructure. Raises
  error in case script not exists in any one of the peer node.
- Filename should start with `peer_` and should exist in
  `$GLUSTER_LIBEXEC` directory.
- This command can not be called from outside the cluster.

To understand the functionality, create a executable file `peer_hello`
under $GLUSTER_LIBEXEC directory and copy to all peer nodes.

    #!/usr/bin/env bash
    echo "Hello from $(gluster system:: uuid get)"

Now run the following command from any one gluster node,

    gluster system:: execute hello

**Note:** Gluster will not copy the executable script to all nodes,
  copy `peer_hello` script to all peer nodes to use `gluster system::
  execute` infrastructure.

It will run `peer_hello` executable in all peer nodes and shows the
output from each node(Below example shows output from my two nodes
cluster)

    Hello from UUID: e7a3c5c8-e7ad-47ad-aa9c-c13907c4da84
    Hello from UUID: c680fc0a-01f9-4c93-a062-df91cc02e40f

## cliutils
A Python wrapper around `gluster system:: execute` command is created
to address the following issues

- If a node is down in the cluster, `system:: execute` just skips it
  and runs only in up nodes.
- `system:: execute` commands are not user friendly
- It captures only stdout, so handling errors is tricky.

**Advantages of cliutils:**

- Single executable file will act as node component as well as User CLI.
- `execute_in_peers` utility function will merge the `gluster system::
  execute` output with `gluster peer status` to identify offline nodes.
- Easy CLI Arguments handling.
- If node component returns non zero return value then, `gluster
  system:: execute` will fail to aggregate the output from other
  nodes. `node_output_ok` or `node_output_notok` utility functions
  returns zero both in case of success or error, but returns json
  with ok: true or ok:false respectively.
- Easy to iterate on the node outputs.
- Better error handling - Geo-rep CLIs `gluster system:: execute
  mountbroker`, `gluster system:: execute gsec_create` and `gluster
  system:: add_secret_pub` are suffering from error handling. These
  tools are not notifying user if any failures during execute or if a node
  is down during execute.

### Hello World
Create a file in `$LIBEXEC/glusterfs/peer_message.py` with following
content.

    #!/usr/bin/python2
    from gluster.cliutils import Cmd, runcli, execute_in_peers, node_output_ok

    class NodeHello(Cmd):
        name = "node-hello"

        def run(self, args):
            node_output_ok("Hello")

    class Hello(Cmd):
        name = "hello"

        def run(self, args):
            out = execute_in_peers("node-hello")
            for row in out:
                print ("{0} from {1}".format(row.output, row.hostname))

    if __name__ == "__main__":
        runcli()

When we run `python peer_message.py`, it will have two subcommands,
"node-hello" and "hello". This file should be copied to
`$LIBEXEC/glusterfs` directory in all peer nodes. User will call
subcommand "hello" from any one peer node, which internally call
`gluster system:: execute message.py node-hello`(This runs in all peer
nodes and collect the outputs)

For node component do not print the output directly, use
`node_output_ok` or `node_output_notok` functions. `node_output_ok`
additionally collects the node UUID and prints in JSON
format. `execute_in_peers` function will collect this output and
merges with `peers list` so that we don't miss the node information if
that node is offline.

If you observed already, function `args` is optional, if you don't
have arguments then no need to create a function. When we run the
file, we will have two subcommands. For example,

    python peer_message.py hello
    python peer_message.py node-hello

First subcommand calls second subcommand in all peer nodes. Basically
`execute_in_peers(NAME, ARGS)` will be converted into

    CMD_NAME = FILENAME without "peers_"
    gluster system:: execute <CMD_NAME> <SUBCOMMAND> <ARGS>

In our example,

    filename = "peer_message.py"
    cmd_name = "message.py"
    gluster system:: execute ${cmd_name} node-hello

Now create symlink in `/usr/bin` or `/usr/sbin` directory depending on
the usecase.(Optional step for usability)

    ln -s /usr/libexec/glusterfs/peer_message.py /usr/bin/gluster-message

Now users can use `gluster-message` instead of calling
`/usr/libexec/glusterfs/peer_message.py`

    gluster-message hello

### Showing CLI output as Table

Following example uses prettytable library, which can be installed
using `pip install prettytable` or `dnf install python-prettytable`

    #!/usr/bin/python2
    from prettytable import PrettyTable
    from gluster.cliutils import Cmd, runcli, execute_in_peers, node_output_ok

    class NodeHello(Cmd):
        name = "node-hello"

        def run(self, args):
            node_output_ok("Hello")

    class Hello(Cmd):
        name = "hello"

        def run(self, args):
            out = execute_in_peers("node-hello")
            # Initialize the CLI table
            table = PrettyTable(["ID", "NODE", "NODE STATUS", "MESSAGE"])
            table.align["NODE STATUS"] = "r"
            for row in out:
                table.add_row([row.nodeid,
                               row.hostname,
                               "UP" if row.node_up else "DOWN",
                               row.output if row.ok else row.error])

            print table

    if __name__ == "__main__":
        runcli()


Example output,

    +--------------------------------------+-----------+-------------+---------+
    |                  ID                  |    NODE   | NODE STATUS | MESSAGE |
    +--------------------------------------+-----------+-------------+---------+
    | e7a3c5c8-e7ad-47ad-aa9c-c13907c4da84 | localhost |          UP |  Hello  |
    | bb57a4c4-86eb-4af5-865d-932148c2759b | vm2       |          UP |  Hello  |
    | f69b918f-1ffa-4fe5-b554-ee10f051294e | vm3       |        DOWN |  N/A    |
    +--------------------------------------+-----------+-------------+---------+

## How to package in Gluster
If the project is created in `$GLUSTER_SRC/tools/message`

Add "message" to SUBDIRS list in `$GLUSTER_SRC/tools/Makefile.am`

and then create a `Makefile.am` in `$GLUSTER_SRC/tools/message`
directory with following content.

    EXTRA_DIST = peer_message.py

    peertoolsdir = $(libexecdir)/glusterfs/
    peertools_SCRIPTS = peer_message.py

    install-exec-hook:
        $(mkdir_p) $(DESTDIR)$(bindir)
        rm -f $(DESTDIR)$(bindir)/gluster-message
        ln -s $(libexecdir)/glusterfs/peer_message.py \
            $(DESTDIR)$(bindir)/gluster-message

    uninstall-hook:
        rm -f $(DESTDIR)$(bindir)/gluster-message

Thats all. Add following files in `glusterfs.spec.in` if packaging is
required.(Under `%files` section)

    %{_libexecdir}/glusterfs/peer_message.py*
    %{_bindir}/gluster-message

## Who is using cliutils
- gluster-mountbroker   http://review.gluster.org/14544
- gluster-eventsapi     http://review.gluster.org/14248
- gluster-georep-sshkey http://review.gluster.org/14732
- gluster-restapi       https://github.com/aravindavk/glusterfs-restapi

## Limitations/TODOs
- Not yet possible to create CLI without any subcommand, For example
  `gluster-message` without any arguments
- Hiding node subcommands in `--help`(`gluster-message --help` will
  show all subcommands including node subcommands)
- Only positional arguments supported for node arguments, Optional
  arguments can be used for other commands.
- API documentation
