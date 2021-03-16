#!/bin/bash

# handle the problem of: GFID mismatch between 'data' and 'linkto' files of a shard file
# - provides a list of all affected shards (where GFID 'data-file' != 'linkto-file')
# - generate a bash script for fixing the affected linkto files

# more details in usage()

#---------------------------------------------------------------------

usage() {
    
    cat <<END
    
    Usage: $0 -v <volume-name> [-f]
    
    options:
        -v <volume-name> gluster volume name to work on (mandatory), 
        -f               run the generated script (optional: don't run)
        -h               usage msg.   

    description:
    Handle GFID mismatch between 'data' and 'linkto' files of a shard file 
    - provides list of all affected shards including the linkto/data gfids and bricks.
      report per subvol is creaded at /tmp/dht/linkto_gfid_mismatch_subvol$i
       example:
        ----------------------------------------------------------------
       #cat linkto_gfid_mismatch_subvol0
        filename      linkto-gfid                         subvol      data-gfid                           subvol  
-------------------------------------------------------------------------------------------------------------------
         f.11,       0x6457a279ff0e4828a021e45f10c1bae3, subvol_0,    0xb71646556c1245b5a6b5c475976e8888, subvol_1     
         f.22,       0x987803f423b84d04bd81136e9bdc6fed, subvol_0,    0xb71646556c1245b5a6b5c475976e8222, subvol_1

    - generate a bash script for fixing the affected linkto files at 
      script per subvol is creaded at /tmp/dht/ FIX_gfid_subvol_0
      can be run manually or by passing -f option
    
    Assumption:
    - script run manually on the client side and has ssh connection to the other's nodes in the cluster.
    - always fix the linkto file gfid.

 
    Flow:

END
    exit
}

log() {
     [ ! -z "$1" ] && echo "$1" >> $WDIR/fix_shard.log
}

#---------------------------------------------------------------------
get_cluster_topo() {
    gluster volume info "$vol" | grep "Number of Bricks" | cut -f2- -d':'
}

get_bricks_from_volume() {
    gluster volume info "$vol" | grep -E "^Brick[0-9][0-9]*:" | cut -f2- -d':' | cut -d ' ' -f2
}

init() {
    num_of_bricks_str=$(get_cluster_topo)
    cluster_topo=($(echo $num_of_bricks_str | grep -o -E '[0-9]+'))
    
    subvols_cnt=${cluster_topo[0]}
    bricks_per_subvol=${cluster_topo[1]}
    
    #support arbiter parsing. e.g. -  2 x (2 + 1) = 6
    if [ "${#cluster_topo[@]}" -gt "3" ]; then
        ((bricks_per_subvol+=${cluster_topo[2]}))
    fi
    
    bricks_str=$(get_bricks_from_volume)
    bricks=($(echo $bricks_str | tr ' ' "\n"))

    printf '%64s\n' | tr ' ' '-'
    echo "Volume $vol topology"
    echo "subvolume_cnt: $subvols_cnt"
    echo "bricks_per_subvol: $bricks_per_subvol"

    printf '%64s\n' | tr ' ' '-'
    for (( i = 0; i < $subvols_cnt; i++ )) 
    do
        echo "subvolume-$i bricks:"
        let start=$i*$bricks_per_subvol 
        let end=($i+1)*$bricks_per_subvol;
        for (( j = $start ; j < $end ; j++ ))
        do
            echo "${bricks[$j]}"
        done
        printf '%64s\n' | tr ' ' '-'
    done
    
    mkdir -p $WDIR; 
}


#----------------------------------------------------
# server-side
#----------------------------------------------------

get_data_file_gfid() {
    
    local file="$1"
    local subvol_idx="$2"
    
    #echo "get_data_file_gfid: file $file  subol $subvol_idx"
      
    let df_brick_0=$subvol_idx*$bricks_per_subvol
    
    local host=$(echo ${bricks[$df_brick_0]} | cut -f1 -d':')
    local bpath=$(echo  ${bricks[$df_brick_0]} | cut -f2 -d':')
    
    #comapre to see if in the same node
    if [ "$local_hostname" = "$host" ] ; then
        echo "Running local CMD: data-file $bpath and linkto-file in the same node."
        data_gfid=$(getfattr -d -m. -e hex $bpath/$shard_dir/$file 2>/dev/null | grep trusted.gfid=|cut -f2 -d'=')
    else
        echo "Running SSH CMD to ${host}"
        data_gfid=$(ssh -n ${host} getfattr -d -m. -e hex $bpath/$shard_dir/$file 2>/dev/null | grep trusted.gfid=|cut -f2 -d'=')
    fi
     
}



logged_GFID_mismatch() {
    
    local f=$1
    local lgfid=$2
    local dgfid=$3
    
    echo "file: $f - GFID linkto file differ from data file. logging."
    printf "%-43s %-35s %-12s %-35s %-12s \n" $f, $lgfid, subvol_$hashed_subvol, $dgfid, subvol_$cached_subvol >> $REPORT_NAME_WIP_FILE$hashed_subvol
    
    #fixing-script
    linkto_gfid_path=".glusterfs/${lgfid:2:2}/${lgfid:4:2}/${lgfid:2:8}-${lgfid:10:4}-${lgfid:14:4}-${lgfid:18:4}-${lgfid:22}" 
    let lf_brick_0=$hashed_subvol*$bricks_per_subvol;
    for (( i = $lf_brick_0; i < $bricks_per_subvol; i++ )); do
        host=$(echo ${bricks[$i]} | cut -d ":" -f 1)
        bpath=$(echo  ${bricks[$i]} | cut -f2 -d':')

cat << EOT >> $FIX_SCRIPT$hashed_subvol

ssh $host << FIX
    cd $bpath/$shard_dir
    setfattr -x "trusted.gfid" $f
    mv ../$linkto_gfid_path /var/log/glusterfs/
    setfattr -n "trusted.gfid" -v $dgfid $file
FIX
EOT
   
    done 
}


run() {

    echo "Testing linkto files in $brick_root:"
    cd $brick_root/$shard_dir
    
#prepare report and fix-script files
    printf "%-43s %-35s %-12s %-35s %-12s \n" filename linkto-gfid subvol data-gfid subvol> $REPORT_NAME_WIP_FILE$hashed_subvol
    printf '%140s\n' | tr ' ' '-' >> $REPORT_NAME_WIP_FILE$hashed_subvol
    
cat <<EOT > $FIX_SCRIPT$hashed_subvol
#! /bin/bash
EOT
    
#iter all linkto files
    getfattr -m trusted.glusterfs.dht.linkto -de text * | 
    while IFS= read -r line; 
    do 
        if [[ ${line:0:6} = "# file" ]] ; then
            file=$(echo "$line" | cut -f3 -d' ')
            continue;   
        fi
        
        if [[ "$line" =~ ^trusted.glusterfs.dht.linkto.* ]]; then
            [ -z "$file" ] && echo "Error while parsing $line, filename missing" && continue;
            
            echo "checking linkto file $brick_root/$shard_dir/$file"            
            let cached_subvol=$(echo $line | awk -F"-" '{print $NF}' |  tr '"\n' ' ')
            get_data_file_gfid $file $cached_subvol
            [ -z "$data_gfid" ] && echo "Error failed to get data_gfid for $file" && continue;
            
            # compare gfid
            link_gfid=$(getfattr -d -m. -e hex $file 2>/dev/null | grep trusted.gfid=|cut -f2 -d'=')
            [[ "$data_gfid" == "$link_gfid" ]] && echo "FILE: $brick_root/$shard_dir/$file. GFID data/linkto files are equal" && continue
            
            logged_GFID_mismatch $file $link_gfid $data_gfid 
                           
        fi
    done
    
    mv $REPORT_NAME_WIP_FILE$hashed_subvol $REPORT_NAME_FILE$hashed_subvol; 
    return 0;
}

#----------------------------------------------------
# main flow functions
#----------------------------------------------------
run_command_on_server()
{
    local host="$1"
    local cmd="$2"
    local output
    output=$(ssh -n "${host}" "${cmd}")
    if [ -n "$output" ]
    then
        echo "node $host ---> end remote run"
        echo "$output"
    fi
}

run_local_command()
{
   local cmd="$1"
   local output
    output=$("${cmd}")
    if [ -n "$output" ]
    then
        echo "$output"
    fi
}


is_local_node() {
    
    local h=$1;
    $(ifconfig | grep -q  $h) && return 0;
    [ $(hostname) == $host ] && return 0;
    
    return 1;
}

#----------------------------------------------------
run_per_subvol() {
    
    for (( i = 0; i < $subvols_cnt; i++ )) 
    do
        b_0=$i*$bricks_per_subvol
        host=$(echo ${bricks[$b_0]} | cut -d ":" -f 1)
        bpath=$(echo ${bricks[$b_0]} | cut -f2 -d':') 
        #if the same host (for the runner script)
        subvol_stat[$i]=0;
        if is_local_node $host; then 
            echo "node $host ---> start local run"
            (./$script_path -v $vol -r $bpath -s $i) &  
        else
            scp $(pwd)/$script_path $host:/tmp/fix_shard_linkto_gfid_$i.sh              
            cmd="/tmp/fix_shard_linkto_gfid_$i.sh -v $vol -r $bpath -s $i"
            echo "node $host ---> start remote run"
            run_command_on_server "${host}" "${cmd}" &
        fi 
    done
}

#----------------------------------------------------------

wait_for_all_subvol() 
{
  
  let timeout=300 #5min
  let iter=0
  let counter=$subvols_cnt;
  
  while [ $counter -gt 0 ]; do
    echo "wait_for_all_subvol ---> waiting"
    ((iter+=5))
    [ $iter == $timeout ] && break
    sleep 5
    
    echo "wait_for_all_subvol ---> start checking subvol" 
    for (( i = 0; i < $subvols_cnt; i++ )); do
        if [ ${subvol_stat[$i]} -eq 1 ]; then 
            echo "subvol $i --> already done"
            continue;
        fi
    
        b_0=$i*$bricks_per_subvol
        host=$(echo ${bricks[$b_0]} | cut -d ":" -f 1)
    
        if is_local_node $host; then 
            if [  -e $REPORT_NAME_FILE$i ];then
                echo "subvol $i -->  detected as done"
                mv $REPORT_NAME_FILE$i $WDIR/${REPORT_NAME}_subvol$i
                subvol_stat[$i]=1;
                ((counter-=1))
            fi        
        else
            if ssh $host "test -e $REPORT_NAME_FILE$i"; then
                echo "subvol $i -->  detected as done"
                scp $host:$REPORT_NAME_FILE$i $WDIR/${REPORT_NAME}_subvol$i
                subvol_stat[$i]=1;
                ((counter-=1))
                if ssh $host "test -e $FIX_SCRIPT$i"; then
                     scp $host:$FIX_SCRIPT$i $FIX_SCRIPT$i
                     chmod +x $FIX_SCRIPT$i
                fi   
            fi
        fi
    done  
  done
    
  if [ $counter -gt 0 ]; then
    echo "wait_for_all_subvol --->  timeout passed stop waiting"
  else
    echo "wait_for_all_subvol --->  all-subvol ended"
  fi
  
  
}

#----------------------------------------------------
run_fix() {
    for f in $FIX_SCRIPT*; do 
        echo "Running fix script $f"; 
        chmod +x $f
        bash -x $f
    done
    
}
#----------------------------------------------------

vol=""
declare -a bricks
subvols_cnt=0
bricks_per_subvol=0
declare -a subvol_stat
brick_root=""
hashed_subvol=0
local_hostname=""
dry_mode==true
shard_dir=".shard"
script_path=""
WDIR="/tmp/dht/"
REPORT_NAME="linkto_gfid_mismatch"
REPORT_NAME_WIP_FILE=$WDIR$REPORT_NAME".work"
REPORT_NAME_FILE=$WDIR$REPORT_NAME".out"
FIX_SCRIPT=$WDIR/"FIX_gfid_subvol_"


while getopts "h?v:r:s:f" opt; do
    case "$opt" in
    h|\?)
        usage "$@"
        exit 0
        ;;
    v)  
       echo "-v was triggered, Parameter: $OPTARG"
       vol=$OPTARG
       ;;
    f)  
       echo "-f was triggered, run in fixing mode"
       dry_mode=false
       ;;
    r)
       echo "-r was triggered, Parameter: $OPTARG"
       brick_root=$OPTARG
       ;;
    s)
       echo "-s was triggered, Parameter: $OPTARG"
       hashed_subvol=$OPTARG
       ;;
    esac
done
if [ $OPTIND -eq 1 ]; then usage "$@"; exit 0; fi

#----------------------------------------------------
echo "$(date +"%T") Running for $vol"
script_path=$0
init;

#runner per subvol
if [ -n "$brick_root" ] && [ -n "$hashed_subvol" ]; then
    echo "Running in subvol_$hashed_subvol"
    local_hostname=$(echo ${bricks[ $hashed_subvol*$bricks_per_subvol]} | cut -f1 -d':') 
    run;
    exit 0;
fi

#main flow
rm -f $WDIR/*;
run_per_subvol;
wait_for_all_subvol;
if [ "$dry_mode" = false ] ; then
    echo "Running fix script"
    run_fix; 
fi    
