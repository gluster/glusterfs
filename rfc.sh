#!/bin/bash -e


branch="release-3.0";


function set_hooks_commit_msg()
{
    f=".git/hooks/commit-msg";
    u="http://review.gluster.com/tools/hooks/commit-msg";

    if [ -x "$f" ]; then
        return;
    fi

    curl -o $f $u || wget -O $f $u;

    chmod +x .git/hooks/commit-msg;
}


function is_num()
{
    local num;

    num="$1";

    [ -z "$(echo $num | sed -e 's/[0-9]//g')" ]
}


function rebase_changes()
{
    git fetch --all;

    GIT_EDITOR=$0 git rebase -i origin/$branch;
}


function editor_mode()
{
    if [ $(basename "$1") = "git-rebase-todo" ]; then
        sed -i 's/^pick /reword /g' "$1";
        return;
    fi

    if [ $(basename "$1") = "COMMIT_EDITMSG" ]; then
        if grep -qi '^BUG: ' $1; then
            return;
        fi
        while true; do
            echo Commit: "\"$(head -n 1 $1)\""
            echo -n "Enter Bug ID: "
            read bug
            if [ -z "$bug" ]; then
                return;
            fi
            if ! is_num "$bug"; then
                echo "Invalid Bug ID ($bug)!!!";
                continue;
            fi

            sed -i "s/^\(Change-Id: .*\)$/\1\nBUG: $bug/g" $1;
            return;
        done
    fi

    cat <<EOF
$0 - editor_mode called on unrecognized file $1 with content:
$(cat $1)
EOF
    return 1;
}


function assert_diverge()
{
    git diff origin/$branch..HEAD | grep -q .;
}


function main()
{
    if [ -e "$1" ]; then
        editor_mode "$@";
        return;
    fi

    set_hooks_commit_msg;

    rebase_changes;

    assert_diverge;

    bug=$(git show --format='%b' | grep -i '^BUG: ' | awk '{print $2}');

    if [ -z "$bug" ]; then
        git push origin HEAD:refs/for/$branch/rfc;
    else
        git push origin HEAD:refs/for/$branch/bug-$bug;
    fi
}

main "$@"
