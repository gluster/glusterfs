# This spec file should be used for testing before any release
# 

# 1st client
volume client1
  type protocol/client
  option transport-type tcp/client     # for TCP/IP transport
# option transport-type ib-sdp/client  # for Infiniband transport
# option transport-type ib-verbs/client # for ib-verbs transport 
# option ibv-send-work-request-size  131072
# option ibv-send-work-request-count 64
# option ibv-recv-work-request-size  131072
# option ibv-recv-work-request-count 64
  option remote-host 127.0.0.1 
  option remote-port 6001
  option remote-subvolume stat1
end-volume

# 2nd client
volume client2
  type protocol/client
  option transport-type tcp/client     # for TCP/IP transport
# option transport-type ib-sdp/client  # for Infiniband transport
# option transport-type ib-verbs/client # for ib-verbs transport 
  option remote-host 127.0.0.1 
  option remote-port 6002
  option remote-subvolume stat2
end-volume

# 3rd client
volume client3
  type protocol/client
  option transport-type tcp/client     # for TCP/IP transport
# option transport-type ib-sdp/client  # for Infiniband transport
# option transport-type ib-verbs/client # for ib-verbs transport 
  option remote-host 127.0.0.1 
  option remote-port 6003
  option remote-subvolume stat3
end-volume

# 4th client
volume client4
  type protocol/client
  option transport-type tcp/client     # for TCP/IP transport
# option transport-type ib-sdp/client  # for Infiniband transport
# option transport-type ib-verbs/client # for ib-verbs transport 
  option remote-host 127.0.0.1 
  option remote-port 6004
  option remote-subvolume stat4
end-volume

# 5th client
volume client5
  type protocol/client
  option transport-type tcp/client     # for TCP/IP transport
# option transport-type ib-sdp/client  # for Infiniband transport
# option transport-type ib-verbs/client # for ib-verbs transport 
  option remote-host 127.0.0.1 
  option remote-port 6005
  option remote-subvolume stat5
end-volume

# 6th client
volume client6
  type protocol/client
  option transport-type tcp/client     # for TCP/IP transport
# option transport-type ib-sdp/client  # for Infiniband transport
# option transport-type ib-verbs/client # for ib-verbs transport 
  option remote-host 127.0.0.1 
  option remote-port 6006
  option remote-subvolume stat6
end-volume

# 7th client
volume client7
  type protocol/client
  option transport-type tcp/client     # for TCP/IP transport
# option transport-type ib-sdp/client  # for Infiniband transport
# option transport-type ib-verbs/client # for ib-verbs transport 
  option remote-host 127.0.0.1 
  option remote-port 6007
  option remote-subvolume stat7
end-volume

# 8th client 
volume client8
  type protocol/client
  option transport-type tcp/client     # for TCP/IP transport
# option transport-type ib-sdp/client  # for Infiniband transport
# option transport-type ib-verbs/client # for ib-verbs transport 
  option remote-host 127.0.0.1 
  option remote-port 6008
  option remote-subvolume stat8
end-volume

# 1st Stripe (client1 client2)
volume stripe1
  type cluster/stripe
  subvolumes client1 client2
  option block-size *  # all striped in 128kB block
end-volume

# 2st Stripe (client3 client4)
volume stripe2
  type cluster/stripe
  subvolumes client3 client4
  option block-size *  # all striped in 128kB block
end-volume

# 3st Stripe (client5 client6)
volume stripe3
  type cluster/stripe
  subvolumes client5 client6
  option block-size *  # all striped in 128kB block
end-volume

# 4st Stripe (client7 client8)
volume stripe4
  type cluster/stripe
  subvolumes client7 client8
  option block-size *  # all striped in 128kB block
end-volume


# 1st AFR
volume afr1
  type cluster/afr
  subvolumes stripe1 stripe2
  option replicate *:2
end-volume

# 2nd AFR
volume afr2
  type cluster/afr
  subvolumes stripe3 stripe4
  option replicate *:2
end-volume

# Unify
volume unify0
  type cluster/unify
  subvolumes afr1 afr2
  option scheduler rr # random # alu # nufa
  option rr.limits.min-disk-free 1GB
# option alu.order x
# option alu.x.entry-threshold
# option alu.x.exit-threshold
end-volume


# ==== Performance Translators ====
# The default options for performance translators should be the best for 90+% of the cases
volume iot
  type performance/io-threads
  subvolumes unify0
end-volume

volume wb
  type performance/write-behind
  subvolumes iot
end-volume

volume ra
  type performance/read-ahead
  subvolumes wb
end-volume

volume stat
  type performance/stat-prefetch
  subvolumes ra
end-volume

