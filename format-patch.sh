#!/bin/bash


function is_num()
{
    local num;

    num="$1";

    [ -z "$(echo $num | sed -e 's/[0-9]//g')" ]
}


function guess_branch()
{
    local branch;
    local src_branch;

    branch=$(git branch | grep '*' | cut -f2 -d' ');

    if [ $branch = "master" ] ; then
        src_branch="master";
    else
        src_branch=$(cat .git/logs/refs/heads/$branch | head -n 1 \
            | sed -r -e 's/.*( [^ ]*)$/\1/g' | cut -f2 -d/);
    fi

    echo $src_branch
}


function main()
{
    local branch;
    local bug;

    branch=$(guess_branch);
    echo
    echo "Patches are always to be associated with a bug ID. If there is no   "
    echo "bug filed in bugzilla for this patch, it is highly suggested to file"
    echo "a new bug with a description and reasoning of this patchset. If this"
    echo "is a new feature, then file a new enhancement bug with a brief      "
    echo "summary of the feature as the description."
    echo
    echo -n "Enter bug ID (from http://bugs.gluster.com/): "
    read bug;

    [ -z "$bug" ] || is_num $bug || {
        log "bug ID should be a valid bug number";
        exit;
    }

    if [ -z "$bug" ]; then
        git format-patch -s "$@";
    else
        git format-patch -s --subject-prefix="PATCH BUG:$bug" "$@";
    fi
}

main "$@"
