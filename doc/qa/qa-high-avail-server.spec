
# -- server 1 --
volume server1-posix1
 type storage/posix
 option directory /tmp/ha-export1/
end-volume

volume server1-ns1
 type storage/posix
 option directory /tmp/ha-export-ns1/
end-volume

volume server1-client2
 type protocol/client
 option transport-type tcp/client
 option remote-host 127.0.0.1
 option remote-port 7002
 option remote-subvolume server2-posix2
end-volume

volume server1-ns2
 type protocol/client
 option transport-type tcp/client
 option remote-host 127.0.0.1
 option remote-port 7002
 option remote-subvolume server2-ns2
end-volume

volume server1-client3
 type protocol/client
 option transport-type tcp/client
 option remote-host 127.0.0.1
 option remote-port 7003
 option remote-subvolume server3-posix3
end-volume

volume server1-ns3
 type protocol/client
 option transport-type tcp/client
 option remote-host 127.0.0.1
 option remote-port 7003
 option remote-subvolume server3-ns3
end-volume

volume server1-io1
 type performance/io-threads
 option thread-count 8
 subvolumes server1-posix1
end-volume


volume server1-io2
 type performance/io-threads
 option thread-count 8
 subvolumes server1-client2
end-volume

volume server1-io3
 type performance/io-threads
 option thread-count 8
 subvolumes server1-client3
end-volume

volume server1-ns-io1
 type performance/io-threads
 option thread-count 8
 subvolumes server1-ns1
end-volume

volume server1-ns-io2
 type performance/io-threads
 option thread-count 8
 subvolumes server1-ns2
end-volume

volume server1-ns-io3
 type performance/io-threads
 option thread-count 8
 subvolumes server1-ns3
end-volume

volume server1-ns-afr
 type cluster/afr
 subvolumes server1-ns-io1 server1-ns-io2 server1-ns-io3
end-volume

volume server1-storage-afr
 type cluster/afr
 subvolumes server1-io1 server1-io2 server1-io3
end-volume

volume server1-unify
 type cluster/unify
 #option self-heal off
 subvolumes server1-storage-afr
 option namespace server1-ns-afr
 option scheduler rr
end-volume

volume server1-iot
 type performance/io-threads
 option thread-count 8
 subvolumes server1-unify
end-volume

volume server1
 type protocol/server
 option transport-type tcp/server
 subvolumes server1-iot
 option listen-port 7001
 option auth.addr.server1-posix1.allow *
 option auth.addr.server1-ns1.allow *
 option auth.addr.server1-iot.allow * 
end-volume


# == Server2 ==
volume server2-client1
 type protocol/client
 option transport-type tcp/client
 option remote-host 127.0.0.1
 option remote-port 7001
 option remote-subvolume server1-posix1
end-volume

volume server2-ns1
 type protocol/client
 option transport-type tcp/client
 option remote-host 127.0.0.1
 option remote-port 7001
 option remote-subvolume server1-ns1
end-volume

volume server2-posix2
 type storage/posix
 option directory /tmp/ha-export2/
end-volume

volume server2-ns2
 type storage/posix
 option directory /tmp/ha-export-ns2/
end-volume

volume server2-client3
 type protocol/client
 option transport-type tcp/client
 option remote-host 127.0.0.1
 option remote-port 7003
 option remote-subvolume server3-posix3
end-volume

volume server2-ns3
 type protocol/client
 option transport-type tcp/client
 option remote-host 127.0.0.1
 option remote-port 7003
 option remote-subvolume server3-ns3
end-volume

volume server2-io1
 type performance/io-threads
 option thread-count 8
 subvolumes server2-client1
end-volume


volume server2-io2
 type performance/io-threads
 option thread-count 8
 subvolumes server2-posix2
end-volume

volume server2-io3
 type performance/io-threads
 option thread-count 8
 subvolumes server2-client3
end-volume

volume server2-ns-io1
 type performance/io-threads
 option thread-count 8
 subvolumes server2-ns1
end-volume

volume server2-ns-io2
 type performance/io-threads
 option thread-count 8
 subvolumes server2-ns2
end-volume

volume server2-ns-io3
 type performance/io-threads
 option thread-count 8
 subvolumes server2-ns3
end-volume

volume server2-ns-afr
 type cluster/afr
 subvolumes server2-ns-io1 server2-ns-io2 server2-ns-io3
end-volume

volume server2-storage-afr
 type cluster/afr
 subvolumes server2-io2 server2-io3 server2-io1
end-volume

volume server2-unify
 type cluster/unify
 option self-heal off
 subvolumes server2-storage-afr
 option namespace server2-ns-afr
 option scheduler rr
end-volume

volume server2-iot
 type performance/io-threads
 option thread-count 8
 option cache-size 64MB
 subvolumes server2-unify
end-volume

volume server2
 type protocol/server
 option transport-type tcp/server
 subvolumes server2-iot
 option listen-port 7002
 option auth.addr.server2-posix2.allow *
 option auth.addr.server2-ns2.allow *
 option auth.addr.server2-iot.allow * 
end-volume

# == server 3 ==
volume server3-client1
 type protocol/client
 option transport-type tcp/client
 option remote-host 127.0.0.1
 option remote-port 7001
 option remote-subvolume server1-posix1
end-volume

volume server3-ns1
 type protocol/client
 option transport-type tcp/client
 option remote-host 127.0.0.1
 option remote-port 7001
 option remote-subvolume server1-ns1
end-volume

volume server3-client2
 type protocol/client
 option transport-type tcp/client
 option remote-host 127.0.0.1
 option remote-port 7002
 option remote-subvolume server2-posix2
end-volume

volume server3-ns2
 type protocol/client
 option transport-type tcp/client
 option remote-host 127.0.0.1
 option remote-port 7002
 option remote-subvolume server2-ns2
end-volume

volume server3-posix3
 type storage/posix
 option directory /tmp/ha-export3/
end-volume

volume server3-ns3
 type storage/posix
 option directory /tmp/ha-export-ns3/
end-volume

volume server3-io1
 type performance/io-threads
 option thread-count 8
 subvolumes server3-client1
end-volume


volume server3-io2
 type performance/io-threads
 option thread-count 8
 subvolumes server3-client2
end-volume

volume server3-io3
 type performance/io-threads
 option thread-count 8
 subvolumes server3-posix3
end-volume

volume server3-ns-io1
 type performance/io-threads
 option thread-count 8
 subvolumes server3-ns1
end-volume

volume server3-ns-io2
 type performance/io-threads
 option thread-count 8
 subvolumes server3-ns2
end-volume

volume server3-ns-io3
 type performance/io-threads
 option thread-count 8
 subvolumes server3-ns3
end-volume

volume server3-ns-afr
 type cluster/afr
 subvolumes server3-ns-io1 server3-ns-io2 server3-ns-io3
end-volume

volume server3-storage-afr
 type cluster/afr
 subvolumes server3-io3 server3-io2 server3-io1
end-volume

volume server3-unify
 type cluster/unify
 option self-heal off
 subvolumes server3-storage-afr
 option namespace server3-ns-afr
 option scheduler rr
end-volume

volume server3-iot
 type performance/io-threads
 option thread-count 8
 option cache-size 64MB
 subvolumes server3-unify
end-volume

volume server3
 type protocol/server
 option transport-type tcp/server
 subvolumes server3-iot
 option listen-port 7003
 option auth.addr.server3-posix3.allow *
 option auth.addr.server3-ns3.allow *
 option auth.addr.server3-iot.allow * 
end-volume

