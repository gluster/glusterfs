#!/bin/sh -e


branch="master";


set_hooks_commit_msg()
{
    f=".git/hooks/commit-msg";
    u="http://review.gluster.com/tools/hooks/commit-msg";

    if [ -x "$f" ]; then
        return;
    fi

    curl -o $f $u || wget -O $f $u;

    chmod +x .git/hooks/commit-msg;

    # Let the 'Change-Id: ' header get assigned on first run of rfc.sh
    GIT_EDITOR=true git commit --amend;
}


is_num()
{
    local num;

    num="$1";

    [ -z "$(echo $num | sed -e 's/[0-9]//g')" ]
}


rebase_changes()
{
    git fetch origin;

    GIT_EDITOR=$0 git rebase -i origin/$branch;
}


editor_mode()
{
    if [ $(basename "$1") = "git-rebase-todo" ]; then
        sed 's/^pick /reword /g' "$1" > $1.new && mv $1.new $1;
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

            sed "/^Change-Id:/{p; s/^.*$/BUG: $bug/;}" $1 > $1.new && \
                mv $1.new $1;
            return;
        done
    fi

    cat <<EOF
$0 - editor_mode called on unrecognized file $1 with content:
$(cat $1)
EOF
    return 1;
}


assert_diverge()
{
    git diff origin/$branch..HEAD | grep -q .;
}


main()
{
    set_hooks_commit_msg;

    if [ -e "$1" ]; then
        editor_mode "$@";
        return;
    fi

    rebase_changes;

    assert_diverge;

    bug=$(git show --format='%b' | grep -i '^BUG: ' | awk '{print $2}');

    if [ "$DRY_RUN" = 1 ]; then
        drier='echo -e Please use the following command to send your commits to review:\n\n'
    else
        drier=
    fi

    if [ -z "$bug" ]; then
        $drier git push origin HEAD:refs/for/$branch/rfc;
    else
        $drier git push origin HEAD:refs/for/$branch/bug-$bug;
    fi
}

main "$@"
