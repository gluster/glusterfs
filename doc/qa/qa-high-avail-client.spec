volume client
 type protocol/client
 option remote-host localhost
 option remote-port 7001
 option remote-subvolume server1-iot
end-volume

volume ra
 type performance/read-ahead
 subvolumes client
end-volume

volume wb
 type performance/write-behind
 subvolumes ra
end-volume
