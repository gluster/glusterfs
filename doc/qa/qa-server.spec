# This spec file should be used for testing before any release
# 

# 1st server

volume brick1
  type storage/posix                   # POSIX FS translator
  option directory /tmp/export1        # Export this directory
end-volume

# == TrashCan Translator ==
# volume trash1
#   type features/trash
#   option trash-dir /.trashcan
#   subvolumes brick1
# end-volume

# == Posix-Locks ==
 volume plocks1
   type features/posix-locks
#  option mandatory on
   subvolumes brick1
 end-volume

volume iot1
  type performance/io-threads
  subvolumes plocks1 # change properly if above commented volumes needs to be included
# option <key> <value>
end-volume

volume wb1
  type performance/write-behind
  subvolumes iot1
# option <key> <value>
end-volume

volume ra1
  type performance/read-ahead
  subvolumes wb1
# option <key> <value>
end-volume

volume stat1
  type performance/stat-prefetch
  subvolumes ra1
# option <key> <value>
end-volume

volume server1
  type protocol/server
  option transport-type tcp/server     # For TCP/IP transport
# option transport-type ib-sdp/server  # For Infiniband transport
# option transport-type ib-verbs/server # For ib-verbs transport
# option ibv-send-work-request-size  131072
# option ibv-send-work-request-count 64
# option ibv-recv-work-request-size  131072
# option ibv-recv-work-request-count 64
  option listen-port 6001              # Default is 6996
  option client-volume-filename /etc/glusterfs/qa-client.vol
  subvolumes stat1
  option auth.ip.stat1.allow * # Allow access to "stat1" volume
end-volume

# ======== 2nd server ===========

volume brick2
  type storage/posix                   # POSIX FS translator
  option directory /tmp/export2        # Export this directory
end-volume

# == TrashCan Translator ==
# volume trash2
#   type features/trash
#   option trash-dir /.trashcan
#   subvolumes brick2
# end-volume

# == Posix-Locks ==
volume plocks2
  type features/posix-locks
#  option <something> <something>
  subvolumes brick2
end-volume

volume iot2
  type performance/io-threads
  subvolumes plocks2 # change properly if above commented volumes needs to be included
# option <key> <value>
end-volume

volume wb2
  type performance/write-behind
  subvolumes iot2
# option <key> <value>
end-volume

volume ra2
  type performance/read-ahead
  subvolumes wb2
# option <key> <value>
end-volume

volume stat2
  type performance/stat-prefetch
  subvolumes ra2
# option <key> <value>
end-volume

volume server2
  type protocol/server
  option transport-type tcp/server     # For TCP/IP transport
# option transport-type ib-sdp/server  # For Infiniband transport
# option transport-type ib-verbs/server # For ib-verbs transport
  option listen-port 6002              # Default is 6996
  subvolumes stat2
  option auth.ip.stat2.allow * # Allow access to "stat1" volume
end-volume


# =========== 3rd server ===========

volume brick3
  type storage/posix                   # POSIX FS translator
  option directory /tmp/export3        # Export this directory
end-volume

# == TrashCan Translator ==
# volume trash3
#   type features/trash
#   option trash-dir /.trashcan
#   subvolumes brick3
# end-volume

# == Posix-Locks ==
volume plocks3
  type features/posix-locks
#  option <something> <something>
  subvolumes brick3
end-volume

volume iot3
  type performance/io-threads
  subvolumes plocks3 # change properly if above commented volumes needs to be included
# option <key> <value>
end-volume

volume wb3
  type performance/write-behind
  subvolumes iot3
# option <key> <value>
end-volume

volume ra3
  type performance/read-ahead
  subvolumes wb3
# option <key> <value>
end-volume

volume stat3
  type performance/stat-prefetch
  subvolumes ra3
# option <key> <value>
end-volume

volume server3
  type protocol/server
  option transport-type tcp/server     # For TCP/IP transport
# option transport-type ib-sdp/server  # For Infiniband transport
# option transport-type ib-verbs/server # For ib-verbs transport
  option listen-port 6003              # Default is 6996
  subvolumes stat3
  option auth.ip.stat3.allow * # Allow access to "stat1" volume
end-volume

# =========== 4th server ===========

volume brick4
  type storage/posix                   # POSIX FS translator
  option directory /tmp/export4        # Export this directory
end-volume

# == TrashCan Translator ==
# volume trash4
#   type features/trash
#   option trash-dir /.trashcan
#   subvolumes brick4
# end-volume

# == Posix-Locks ==
volume plocks4
  type features/posix-locks
# option <something> <something>
  subvolumes brick4
end-volume

volume iot4
  type performance/io-threads
  subvolumes plocks4 # change properly if above commented volumes needs to be included
# option <key> <value>
end-volume

volume wb4
  type performance/write-behind
  subvolumes iot4
# option <key> <value>
end-volume

volume ra4
  type performance/read-ahead
  subvolumes wb4
# option <key> <value>
end-volume

volume stat4
  type performance/stat-prefetch
  subvolumes ra4
# option <key> <value>
end-volume

volume server4
  type protocol/server
  option transport-type tcp/server     # For TCP/IP transport
# option transport-type ib-sdp/server  # For Infiniband transport
# option transport-type ib-verbs/server # For ib-verbs transport
  option listen-port 6004              # Default is 6996
  subvolumes stat4
  option auth.ip.stat4.allow * # Allow access to "stat1" volume
end-volume

# =========== 5th server ===========

volume brick5
  type storage/posix                   # POSIX FS translator
  option directory /tmp/export5        # Export this directory
end-volume

# == TrashCan Translator ==
# volume trash5
#   type features/trash
#   option trash-dir /.trashcan
#   subvolumes brick5
# end-volume

# == Posix-Locks ==
volume plocks5
  type features/posix-locks
# option <something> <something>
  subvolumes brick5
end-volume

volume iot5
  type performance/io-threads
  subvolumes plocks5 # change properly if above commented volumes needs to be included
# option <key> <value>
end-volume

volume wb5
  type performance/write-behind
  subvolumes iot5
# option <key> <value>
end-volume

volume ra5
  type performance/read-ahead
  subvolumes wb5
# option <key> <value>
end-volume

volume stat5
  type performance/stat-prefetch
  subvolumes ra5
# option <key> <value>
end-volume

volume server5
  type protocol/server
  option transport-type tcp/server     # For TCP/IP transport
# option transport-type ib-sdp/server  # For Infiniband transport
# option transport-type ib-verbs/server # For ib-verbs transport
  option listen-port 6005              # Default is 6996
  subvolumes stat5
  option auth.ip.stat5.allow * # Allow access to "stat1" volume
end-volume

# =========== 6th server ===========

volume brick6
  type storage/posix                   # POSIX FS translator
  option directory /tmp/export6        # Export this directory
end-volume

# == TrashCan Translator ==
# volume trash6
#   type features/trash
#   option trash-dir /.trashcan
#   subvolumes brick6
# end-volume

# == Posix-Locks ==
volume plocks6
  type features/posix-locks
#   option <something> <something>
  subvolumes brick6
end-volume

volume iot6
  type performance/io-threads
  subvolumes plocks6 # change properly if above commented volumes needs to be included
# option <key> <value>
end-volume

volume wb6
  type performance/write-behind
  subvolumes iot6
# option <key> <value>
end-volume

volume ra6
  type performance/read-ahead
  subvolumes wb6
# option <key> <value>
end-volume

volume stat6
  type performance/stat-prefetch
  subvolumes ra6
# option <key> <value>
end-volume

volume server6
  type protocol/server
  option transport-type tcp/server     # For TCP/IP transport
# option transport-type ib-sdp/server  # For Infiniband transport
# option transport-type ib-verbs/server # For ib-verbs transport
  option listen-port 6006              # Default is 6996
  subvolumes stat6
  option auth.ip.stat6.allow * # Allow access to "stat1" volume
end-volume

# =========== 7th server ===========

volume brick7
  type storage/posix                   # POSIX FS translator
  option directory /tmp/export7        # Export this directory
end-volume

# == TrashCan Translator ==
# volume trash7
#   type features/trash
#   option trash-dir /.trashcan
#   subvolumes brick7
# end-volume

# == Posix-Locks ==
volume plocks7
  type features/posix-locks
#   option <something> <something>
  subvolumes brick7
end-volume

volume iot7
  type performance/io-threads
  subvolumes plocks7 # change properly if above commented volumes needs to be included
# option <key> <value>
end-volume

volume wb7
  type performance/write-behind
  subvolumes iot7
# option <key> <value>
end-volume

volume ra7
  type performance/read-ahead
  subvolumes wb7
# option <key> <value>
end-volume

volume stat7
  type performance/stat-prefetch
  subvolumes ra7
# option <key> <value>
end-volume

volume server7
  type protocol/server
  option transport-type tcp/server     # For TCP/IP transport
# option transport-type ib-sdp/server  # For Infiniband transport
# option transport-type ib-verbs/server # For ib-verbs transport
  option listen-port 6007              # Default is 6996
  subvolumes stat7
  option auth.ip.stat7.allow * # Allow access to "stat1" volume
end-volume

# =========== 8th server ===========

volume brick8
  type storage/posix                   # POSIX FS translator
  option directory /tmp/export8        # Export this directory
end-volume

# == TrashCan Translator ==
# volume trash8
#   type features/trash
#   option trash-dir /.trashcan
#   subvolumes brick8
# end-volume

# == Posix-Locks ==
volume plocks8
  type features/posix-locks
#   option <something> <something>
  subvolumes brick8
end-volume

volume iot8
  type performance/io-threads
  subvolumes plocks8 # change properly if above commented volumes needs to be included
# option <key> <value>
end-volume

volume wb8
  type performance/write-behind
  subvolumes iot8
# option <key> <value>
end-volume

volume ra8
  type performance/read-ahead
  subvolumes wb8
# option <key> <value>
end-volume

volume stat8
  type performance/stat-prefetch
  subvolumes ra8
# option <key> <value>
end-volume

volume server8
  type protocol/server
  option transport-type tcp/server     # For TCP/IP transport
# option transport-type ib-sdp/server  # For Infiniband transport
# option transport-type ib-verbs/server # For ib-verbs transport
  option listen-port 6008              # Default is 6996
  subvolumes stat8
  option auth.ip.stat8.allow * # Allow access to "stat8" volume
end-volume
