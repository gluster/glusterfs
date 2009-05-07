#!/bin/sh

# Each entry in the array below is a regular expression to match stale keys

xs=("trusted.glusterfs.createtime"
    "trusted.glusterfs.version"
    "trusted.glusterfs.afr.data-pending"
    "trusted.glusterfs.afr.metadata-pending"
    "trusted.glusterfs.afr.entry-pending")
    
absolute_path()
{
    local dir=$(dirname "$1");
    local base=$(basename "$1");
    cd "$dir";
    echo $(pwd)/"$base";
    cd - >/dev/null;
}


cleanup()
{
    sanitizee=$(absolute_path "$1");

    stale_keys=$(
        for pattern in ${xs[@]}; do
            getfattr -d -m "$pattern" "$sanitizee" 2>/dev/null |
            grep = | cut -f1 -d=;
        done
        )

    if [ -z "$stale_keys" ]; then
        return;
    fi

    for key in $stale_keys; do
        echo "REMOVEXATTR ($key) $sanitizee";
        setfattr -x "$key" "$sanitizee";
    done
}


crawl()
{
    this_script=$(absolute_path "$0");

    export sanitize=yes;
    find "$1" -exec "$this_script" {} \;
}


main()
{
    if [ -z "$1" ]; then
        echo "Usage: $0 <export-dir>"
        return 1
    fi

    if [ "$sanitize" = "yes" ]; then
        cleanup "$@";
    else
        crawl "$@";
    fi
}

main "$@"
